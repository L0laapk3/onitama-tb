export module tb:tablebase;
import std;
import :types;
import :card;
import :board;
import :index;
import :sync;

namespace tablebase_impl {

struct ThreadObj {
	Sync sync;
	U64 iteration = 1;
	std::atomic<bool> updated = true;
};

constexpr U64 CHUNK_P0_POSITIONS = 1;

constexpr bool VERBOSE = true;


// STEP 1: no TB lookups, just check win in 0/invalid boards & win in 1.
// STEP 2: TB lookups.
template <U16 TB_MEN, U32 ROW, int STEP>
void processRow(const CardsInfo& cards, auto& tb, U64& chunk, U64& rowStartChunk, std::atomic<U64>& chunkCounter, bool& updated) {
	auto& row = std::get<ROW>(tb);
	constexpr U64 P0_CHUNKS = (row.size() - 1) / CHUNK_P0_POSITIONS + 1;

	constexpr auto PC = PIECE_COUNTS<TB_MEN>[ROW];
	constexpr int MIRROR_ROW = rowIndex<TB_MEN>(PC.p1c, PC.p0c);
	constexpr int P0_TAKEN_ROW = rowIndex<TB_MEN>(PC.p1c - 1, PC.p0c);
	constexpr int P1_UNTAKEN_ROW = rowIndex<TB_MEN>(PC.p1c, PC.p0c + 1);

	for (; chunk < rowStartChunk + P0_CHUNKS; chunk = chunkCounter++) {
		const U32 begin = static_cast<U32>((chunk - rowStartChunk) * CHUNK_P0_POSITIONS);
		const U32 end = std::min<U32>(begin + CHUNK_P0_POSITIONS, static_cast<U32>(row.size()));
		for (int ip0 = begin; ip0 < end; ip0++) {
			auto& rowP0 = row[ip0];
			const U32 bbp1 = unrankFirstPieces<TB_MEN, ROW, true>(ip0);
			const U32 bbp0_inv = unrankFirstPieces<TB_MEN, ROW, false>(ip0);
			for (int ip1 = 0; ip1 < static_cast<int>(rowP0.size()); ip1++) {
				auto& rowP1 = rowP0[ip1];
				const U32 bbp0 = unrankSecondPieces<TB_MEN, ROW, true>(ip1, bbp1);
				const U32 bbp1_inv = unrankSecondPieces<TB_MEN, ROW, false>(ip1, bbp0_inv);
				const int ip0_new = rankFirstPieces<TB_MEN, MIRROR_ROW, false>(bbp0);
				for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
					auto& rowK0 = rowP1[ik0];
					const U32 bbk1 = unrankFirstKing<TB_MEN, ROW, true>(ik0, bbp1);
					const U32 bbk0_inv = unrankFirstKing<TB_MEN, ROW, false>(ik0, bbp0_inv);
					for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++) {
						auto& cardsEntry = rowK0[ik1];
						const U32 bbk0 = unrankSecondKing<TB_MEN, ROW, true>(ik1, bbp0);
						const U32 bbk1_inv = unrankSecondKing<TB_MEN, ROW, false>(ik1, bbp1_inv);
						const int ik0_new = rankFirstKing<TB_MEN, MIRROR_ROW, false>(bbk0, bbp0);

						U32 entry;
						if constexpr (STEP == 1) {
							// Other threads may already have reverse marked bits of this entry as win in 3: clear with
							// RMWs to keep those marks.
							Board board{ bbp0, bbp1, bbk0, bbk1 };
							Board inverseBoard{ bbp0_inv, bbp1_inv, bbk0_inv, bbk1_inv };
							if (board.isTempleEnded() || inverseBoard.isTempleEnded()) { // Win in 0
								cardsEntry.store(CARD_PERMS_MASK, std::memory_order_relaxed);
								continue;
							}
							const U32 winInOneCards = inverseBoard.getWinInOneCards<0>(cards.moveBoardsReverse);
							entry = ~(cardsEntry.fetch_or(winInOneCards, std::memory_order_relaxed) | winInOneCards) & CARD_PERMS_MASK;
						} else {
							// loop over all entries, when a stored bit is 0 that means the entry is still unresolved.
							if (!(entry = ~cardsEntry.load(std::memory_order_relaxed) & CARD_PERMS_MASK))
								continue;
						}
						U32 newEntries = 0;
						U32 unresolvedAfterMove = 0;

						{ // forwards movegen - check if all possible p1 moves are resolved
							U32 sourcePieces = bbp1 & ~bbk0; // No need to check king takes ;)
							for (int iSrc = 0; iSrc < PC.p0c; iSrc++) {
								const U32 sourcePiece = sourcePieces & -sourcePieces;
								int pp = std::countr_zero(sourcePieces);
								sourcePieces &= sourcePieces - 1;
								const U32 bbp1_without_source = bbp1 - sourcePiece;
								U32 landPieces = cards.moveBoardsReverse.all[pp] & ~bbp1; // can't land on my own pieces
								while (landPieces) {
									const U32 landPiece = landPieces & -landPieces;
									landPieces &= landPieces - 1;
									const U32 bbp1_new = bbp1_without_source | landPiece;
									const U32 bbk1_new = sourcePiece == bbk1 ? landPiece : bbk1;

									if constexpr (STEP == 1) {
										Board board{
											.bbp = { bbp0 & ~landPiece, bbp1_new },
											.bbk = { bbk0, bbk1_new },
										};
										const U32 unresolvedChild = ~board.getWinInOneCards<0>(cards.moveBoardsReverse);
										unresolvedAfterMove |= unresolvedChild & cards.moveBoardsReverse.sideCards[pp][std::countr_zero(landPiece)];
									} else {
										const bool isTakeMove = landPiece & bbp0;
										U32 otherEntry; // resolved bits of the child
										if (!isTakeMove) {
											const int ip1_new = rankSecondPieces<TB_MEN, MIRROR_ROW, false>(bbp1_new, bbp0);
											const int ik1_new = rankSecondKing<TB_MEN, MIRROR_ROW, false>(bbk1_new, bbp1_new);
											otherEntry = std::get<MIRROR_ROW>(tb)[ip0_new][ip1_new][ik0_new][ik1_new].load(std::memory_order_acquire);
										} else {
											if constexpr (PC.p1c == 1) {
												otherEntry = CARD_PERMS_MASK; // when p0 only has its king left, every take is a king take
											} else {
												const U32 bbp0_taken = bbp0 & ~landPiece;
												const int ip0_taken = rankFirstPieces<TB_MEN, P0_TAKEN_ROW, false>(bbp0_taken);
												const int ip1_taken = rankSecondPieces<TB_MEN, P0_TAKEN_ROW, false>(bbp1_new, bbp0_taken);
												const int ik0_taken = rankFirstKing<TB_MEN, P0_TAKEN_ROW, false>(bbk0, bbp0_taken);
												const int ik1_taken = rankSecondKing<TB_MEN, P0_TAKEN_ROW, false>(bbk1_new, bbp1_new);
												otherEntry = std::get<P0_TAKEN_ROW>(tb)[ip0_taken][ip1_taken][ik0_taken][ik1_taken].load(std::memory_order_acquire);
											}
										}

										unresolvedAfterMove |= ~otherEntry & cards.moveBoardsReverse.sideCards[pp][std::countr_zero(landPiece)];
									}
								}
							}
						}
						newEntries |= unmoveCardEntry(unresolvedAfterMove);

						newEntries &= entry;
						if (entry == newEntries) // all unresolved entries survived, nothing to update
							continue;

						// Reload after the acquire loads of the children: a child that was seen resolved as a loss has
						// already marked its parents (this entry) before releasing, so those win bits must not count as lost.
						if constexpr (STEP != 1)
							entry &= ~cardsEntry.load(std::memory_order_acquire);
						const U32 lost = entry & ~newEntries;
						if (!lost)
							continue;

						{ // reverse movegen - all entries that can reach this entry are also marked as resolved.
							U32 sourcePieces = bbp0;
							for (int iSrc = 0; iSrc < PC.p1c; iSrc++) {
								const U32 sourcePiece = sourcePieces & -sourcePieces;
								int pp = std::countr_zero(sourcePieces);
								sourcePieces &= sourcePieces - 1;
								const U32 bbp0_without_source = bbp0 - sourcePiece;
								U32 landPieces = cards.moveBoardsReverse.all[pp] & ~(bbp0 | bbp1);
								while (landPieces) {
									const U32 landPiece = landPieces & -landPieces;
									landPieces &= landPieces - 1;
									const U32 bbp0_new = bbp0_without_source | landPiece;
									const U32 bbk0_new = sourcePiece == bbk0 ? landPiece : bbk0;

									const int ip0_new = rankFirstPieces<TB_MEN, MIRROR_ROW, false>(bbp0_new);
									const int ip1_new = rankSecondPieces<TB_MEN, MIRROR_ROW, false>(bbp1, bbp0_new); // TODO incremental?
									const int ik0_new = rankFirstKing<TB_MEN, MIRROR_ROW, false>(bbk0_new, bbp0_new);
									const int ik1_new = rankSecondKing<TB_MEN, MIRROR_ROW, false>(bbk1, bbp1);

									const U32 newEntryBits = unmoveCardEntry(lost & cards.moveBoardsReverse.sideCards[pp][std::countr_zero(landPiece)]);
									std::get<MIRROR_ROW>(tb)[ip0_new][ip1_new][ik0_new][ik1_new].fetch_or(newEntryBits, std::memory_order_relaxed);

									if constexpr (P1_UNTAKEN_ROW >= 0) {
										const U32 bbp1_untaken = bbp1 | sourcePiece;
										const int ip0_untaken = rankFirstPieces<TB_MEN, P1_UNTAKEN_ROW, false>(bbp0_new);
										const int ip1_untaken = rankSecondPieces<TB_MEN, P1_UNTAKEN_ROW, false>(bbp1_untaken, bbp0_new); // TODO incremental?
										const int ik0_untaken = rankFirstKing<TB_MEN, P1_UNTAKEN_ROW, false>(bbk0_new, bbp0_new);
										const int ik1_untaken = rankSecondKing<TB_MEN, P1_UNTAKEN_ROW, false>(bbk1, bbp1_untaken);
										std::get<P1_UNTAKEN_ROW>(tb)[ip0_untaken][ip1_untaken][ik0_untaken][ik1_untaken].fetch_or(newEntryBits, std::memory_order_relaxed);
									}
								}
							}
						}

						// Only after the reverse movegen, so a thread that sees these bits set also sees the parents marked.
						cardsEntry.fetch_or(lost, std::memory_order_release);
						updated = true;
					}
				}
			}
		}
	}
	rowStartChunk += P0_CHUNKS;
}

template <U16 TB_MEN, int STEP, typename Storage>
void singleDepthPass(const CardsInfo& cards, Storage& tb, std::atomic<U64>& chunkCounter, bool& updated) {
	U64 chunk = chunkCounter++;
	U64 rowStartChunk = 0;

	[&]<U32... ROW>(std::integer_sequence<U32, ROW...>) {
		(processRow<TB_MEN, ROW, STEP>(cards, tb, chunk, rowStartChunk, chunkCounter, updated), ...);
	}(std::make_integer_sequence<U32, std::tuple_size_v<Storage>>{});
}

template <typename Storage>
U64 countResolved(const Storage& tb) {
	// Only called while the workers are idle, so the entries are read as plain U64 words.
	constexpr U64 CHUNK_WORDS = 1 << 16;
	std::vector<std::span<const U64>> chunks;
	U64 tailCount = 0;
	std::apply([&](const auto&... rows) {
		([&](const auto& row) {
			const U64 entries = row.size() * sizeof(row[0]) / sizeof(U32);
			const U64* words = reinterpret_cast<const U64*>(row.data());
			for (U64 i = 0; i < entries / 2; i += CHUNK_WORDS)
				chunks.emplace_back(words + i, std::min(CHUNK_WORDS, entries / 2 - i));
			if (entries % 2)
				tailCount += std::popcount(reinterpret_cast<const U32*>(words)[entries - 1]);
		}(rows), ...);
	}, tb);

	std::atomic<U64> nextChunk = 0;
	std::atomic<U64> count = tailCount;
	{
		std::vector<std::jthread> threads;
		for (unsigned i = 0; i < std::max(1u, std::thread::hardware_concurrency()); i++)
			threads.emplace_back([&] {
				U64 local = 0;
				for (U64 c; (c = nextChunk++) < chunks.size();)
					for (const U64 word : chunks[c])
						local += std::popcount(word);
				count += local;
			});
	}
	return count;
}

template <U16 TB_MEN, U32... ROW>
consteval U64 countTotal(std::integer_sequence<U32, ROW...>) {
	return (0ULL + ... + (30ULL
		* PAWNTABLE_P0<TB_MEN, ROW>.size()
		* PAWNTABLE_P1<TB_MEN, ROW>.size()
		* PIECE_COUNTS<TB_MEN>[ROW].p0c
		* PIECE_COUNTS<TB_MEN>[ROW].p1c));
}

template <U16 TB_MEN, typename Storage>
void singleThread(const CardsInfo& cards, Storage& tb, std::atomic<U64>& chunkCounter, ThreadObj& comm) {
	while (true) {
		comm.sync.slaveNotifyWait();
		if (comm.iteration == 0)
			break;

		bool updated = false;
		if (comm.iteration == 1)
			singleDepthPass<TB_MEN, 1>(cards, tb, chunkCounter, updated);
		else
			singleDepthPass<TB_MEN, 2>(cards, tb, chunkCounter, updated);
		if (updated)
			comm.updated.store(true, std::memory_order_relaxed);
	}
}

template <U16 TB_MEN, typename Storage>
void runTableBaseBuild(const CardsInfo& cards, Storage& tb, U64 stopAtIteration, std::chrono::steady_clock::time_point startTime) {
	// 30 card perms. 47 perms with kings on their temple. times all combinations of zero to 2 pawns on each side
	// constexpr U64 EXPECTED_WIN_IN_ZERO = 30 * 47 * (1 + 23 + 23*22/2 + 23 * (1 + 22 + 22*21/2) + 23*22/2 * (1 + 21 + 21*20/2));
	constexpr U64 EXPECTED_RESOLVED_STATES = TB_MEN == 6 ? 1166580494ULL : 50958224689ULL;

	std::atomic<U64> chunkCounter;
	ThreadObj comm;
	int numThreads = std::clamp<int>(static_cast<int>(std::thread::hardware_concurrency()), 1, 1024);
	std::vector<std::thread> threads(numThreads);
	for (int i = 0; i < numThreads; i++)
		threads[i] = std::thread(singleThread<TB_MEN, Storage>, std::cref(cards), std::ref(tb), std::ref(chunkCounter), std::ref(comm));
	comm.sync.masterWait(numThreads);

	std::chrono::duration<double> countingTime{};
	U64 resolvedStates = 0;
	U64 newResolvedStates = 1;
	constexpr U64 total = countTotal<TB_MEN>(std::make_integer_sequence<U32, PIECE_COUNTS<TB_MEN>.size()>{});
	while (comm.updated && comm.iteration <= stopAtIteration) {
		chunkCounter = 0;
		comm.updated = false;
		const auto iterationStart = std::chrono::steady_clock::now();
		comm.sync.masterNotify(numThreads);
		comm.sync.masterWait(numThreads);
		const auto countingStart = std::chrono::steady_clock::now();
		const std::chrono::duration<double> iterationTime = countingStart - iterationStart;

		if constexpr (VERBOSE) {
			const U64 count = countResolved(tb);
			newResolvedStates = count - resolvedStates;
			resolvedStates = count;
			std::cout << std::format("it {:3}: {:12} ({:.4f}%) in {:.2f} seconds\n", comm.iteration, newResolvedStates, 100.0 * resolvedStates / total, iterationTime.count());
		} else
			std::cout << "." << std::flush;

		countingTime += std::chrono::steady_clock::now() - countingStart;
		comm.iteration++;
	}

	const std::chrono::duration<double> totalTime = std::chrono::steady_clock::now() - startTime - countingTime;
	std::cout << std::format("\ntotal {}-men: {} states in {:.2f} seconds (+{:.2f} seconds counting)\n", TB_MEN, resolvedStates, totalTime.count(), countingTime.count());

	if constexpr (!VERBOSE)
		resolvedStates = countResolved(tb);
	if (resolvedStates != EXPECTED_RESOLVED_STATES) {
		std::cerr << "ERROR: WRONG NUMBER OF RESOLVED BOARDS (got " << resolvedStates << ", expected " << EXPECTED_RESOLVED_STATES << ")\n";
		throw std::runtime_error("wrong number of boards");
	}

	comm.iteration = 0;
	comm.sync.masterNotify(numThreads);
	for (auto& thread : threads)
		thread.join();
}

} // namespace tablebase_impl

export template <U16 TB_MEN>
struct TableBase {
	using CardsEntry = std::atomic<U32>;

	template <U16 ROW_I>
	using TableKingPermsP1 = std::array<CardsEntry, PIECE_COUNTS<TB_MEN>[ROW_I].p1c>;

	template <U16 ROW_I>
	using TableKingPermsP0 = std::array<TableKingPermsP1<ROW_I>, PIECE_COUNTS<TB_MEN>[ROW_I].p0c>;

	template <U16 ROW_I>
	using TableP1 = std::array<TableKingPermsP0<ROW_I>, PAWNTABLE_P1<TB_MEN, ROW_I>.size()>;

	template <U16 ROW_I>
	struct alignas(64) TableRow : std::array<TableP1<ROW_I>, PAWNTABLE_P0<TB_MEN, ROW_I>.size()> {};

	template <std::size_t... I>
	static auto tableBaseStorage(std::index_sequence<I...>) -> std::tuple<TableRow<I>...>;

	using TableBaseStorage = decltype(tableBaseStorage(std::make_index_sequence<PIECE_COUNTS<TB_MEN>.size()>{}));

	explicit TableBase(const CardsInfo& cards, U64 stopAtIteration = std::numeric_limits<U64>::max()) {
		const auto allocStart = std::chrono::steady_clock::now();
		tb = std::make_unique<TableBaseStorage>();
		const std::chrono::duration<double> allocTime = std::chrono::steady_clock::now() - allocStart;
		std::cout << std::format("allocated {:.1f}GB in {:.2f} seconds\n", sizeof(TableBaseStorage) / 1e9, allocTime.count());
		tablebase_impl::runTableBaseBuild<TB_MEN>(cards, *tb, stopAtIteration, allocStart);
	}

	std::unique_ptr<TableBaseStorage> tb;
};
