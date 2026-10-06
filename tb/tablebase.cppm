export module tb:tablebase;
import std;
import :types;
import :card;
import :board;
import :index;
import :sync;

export template <U16 TB_MEN>
struct TableBase;

struct ThreadObj {
	Sync sync;
	U64 iteration = 1;
	std::atomic<bool> updated = true;
};

constexpr U64 CHUNK_P0_POSITIONS = 1;
constexpr bool VERBOSE = true;

// STEP 1: no TB lookups, just check win in 0/invalid boards & win in 1.
// STEP 2: TB lookups.
template <U16 TB_MEN, U16 P0C, U16 P1C, int STEP>
void processRow(const CardsInfo& cards, auto& tb, U64& chunk, U64& rowStartChunk, std::atomic<U64>& chunkCounter, bool& updated) {
	auto& row = std::get<rowIndex<TB_MEN>(P0C, P1C)>(tb);

	// Iterate in mirrored <P1C, P0C> order: the outer loop fixes p1's pieces, so all non-take children share the mirrored row at ip0_new.
	constexpr U32 OUTER_SIZE = PAWNTABLE_P0<P1C, P0C>.size();
	constexpr U32 INNER_SIZE = PAWNTABLE_P1<P1C, P0C>.size();
	constexpr U64 P0_CHUNKS = (OUTER_SIZE - 1) / CHUNK_P0_POSITIONS + 1;

	for (; chunk < rowStartChunk + P0_CHUNKS; chunk = chunkCounter++) {
		const U32 begin = static_cast<U32>((chunk - rowStartChunk) * CHUNK_P0_POSITIONS);
		const U32 end = std::min<U32>(begin + CHUNK_P0_POSITIONS, OUTER_SIZE);
		for (int ip0_new = begin; ip0_new < end; ip0_new++) { // loop over the outer table in an order that maximizes locality for forward move generation
			const U32 bbp1 = unrankFirstPieces<true, P1C, P0C>(ip0_new);
			std::array<int, P1C> ip0s_taken; // precalculate all the ranks for different taken pieces
			if constexpr (P1C > 1) {
				U32 bbp1_source = bbp1;
				for (int i = 0; i < P1C; i++) {
					const U32 pp1 = bbp1_source & -bbp1_source;
					bbp1_source &= bbp1_source - 1;
					ip0s_taken[i] = rankFirstPieces<true, P1C - 1, P0C>(bbp1 - pp1);
				}
			}
			for (int ipInner = 0; ipInner < static_cast<int>(INNER_SIZE); ipInner++) {
				const U32 bbp0 = unrankSecondPieces<true, P1C, P0C>(ipInner, bbp1);
				auto& rowP1 = row[rankFirstPieces<false, P0C, P1C>(bbp0)][rankSecondPieces<false, P0C, P1C>(bbp1, bbp0)];
				auto* it = &rowP1[0][0];
				{ // Optimization: If the entire block of king perms is empty, continue early
					auto* entryIt = it;
					bool blockHasUnresolved = false; // STEP 1 has to initialize every entry of the block before jumping
					for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
						auto& rowK0 = rowP1[ik0];
						const U32 bbk0 = unrankFirstKing<false, P0C, P1C>(ik0, bbp0);
						for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++) {

							auto& cardsEntry = *(entryIt++);
							U32 entry;
							if constexpr (STEP == 1) {
								const U32 bbk1 = unrankSecondKing<false, P0C, P1C>(ik1, bbp1);
								// Other threads may already have reverse marked bits of this entry as win in 3: clear with RMWs to keep those marks.
								Board board{ bbp0, bbp1, bbk0, bbk1 };
								if (board.isTempleEnded()) { // Win in 0
									cardsEntry.store(0, std::memory_order_relaxed);
									entry = 0; // Win in 0 - skip all the forwards and backwards movegen
								} else {
									const U32 winInOneCards = board.getWinInOneCards<0>(cards.moveBoardsReverse);
									entry = cardsEntry.load(std::memory_order_relaxed);
									if (entry & winInOneCards)
										entry = cardsEntry.fetch_and(~winInOneCards, std::memory_order_relaxed) & ~winInOneCards;
								}
							} else {
								// loop over all entries, when a stored bit is 1 that means the entry is still unresolved.
								entry = cardsEntry.load(std::memory_order_relaxed);
							}
							if (entry) {
								if constexpr (STEP > 1)
									goto BlockHasUnresolved;
								blockHasUnresolved = true;
							}
						}
					}
					if (blockHasUnresolved)
						goto BlockHasUnresolved;
					continue;
					BlockHasUnresolved:
				}

				std::array<U32, P0C * P1C> unresolvedChildren{0};
				{ // forwards movegen - check if all possible p0 moves are resolved
					U32 sourcePieces = bbp0;
					for (int iSrc = 0; iSrc < P0C; iSrc++) {
						const U32 sourcePiece = sourcePieces & -sourcePieces;
						int pp = std::countr_zero(sourcePieces);
						sourcePieces &= sourcePieces - 1;
						const U32 bbp0_without_source = bbp0 - sourcePiece;
						U32 landPieces = cards.moveBoardsForward.all[pp] & ~bbp0; // can't land on my own pieces
						while (landPieces) {
							const U32 landPiece = landPieces & -landPieces;
							landPieces &= landPieces - 1;
							const U32 bbp0_new = bbp0_without_source | landPiece;

							if constexpr (STEP == 1) {
								auto* entryIt = it;
								auto childIt = unresolvedChildren.begin();
								for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
									auto& rowK0 = rowP1[ik0];
									const U32 bbk0 = unrankFirstKing<false, P0C, P1C>(ik0, bbp0);
									const U32 bbk0_new = sourcePiece == bbk0 ? landPiece : bbk0;
									for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++, entryIt++, childIt++) {
										if (!entryIt->load(std::memory_order_relaxed)) // already resolved, e.g. win in 0
											continue;
										const U32 bbk1 = unrankSecondKing<false, P0C, P1C>(ik1, bbp1);
										if (landPiece == bbk1) // King takes are obviously resolved
											continue;

										Board board{
											.bbp = { bbp0_new, bbp1 & ~landPiece },
											.bbk = { bbk0_new, bbk1 },
										};
										const U32 unresolvedChild = ~board.getWinInOneCards<1>(cards.moveBoardsForward);
										*childIt |= unresolvedChild & cards.moveBoardsForward.sideCards[pp][std::countr_zero(landPiece)];
									}
								}
							} else {
								const bool isTakeMove = landPiece & bbp1;
								std::array<U32, P0C * P1C> otherEntry; // unresolved bits of the child
								auto otherIt = otherEntry.begin();
								if (!isTakeMove) {
									const int ip1_new = rankSecondPieces<true, P1C, P0C>(bbp0_new, bbp1);
									const auto& pawnRow_new = std::get<rowIndex<TB_MEN>(P1C, P0C)>(tb)[ip0_new][ip1_new];
									__builtin_prefetch(pawnRow_new.data(), 0, 0);

									for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
										auto& rowK0 = rowP1[ik0];
										const U32 bbk0 = unrankFirstKing<false, P0C, P1C>(ik0, bbp0);
										const U32 bbk0_new = sourcePiece == bbk0 ? landPiece : bbk0;
										const U32 ik1_new = rankSecondKing<true, P1C, P0C>(bbk0_new, bbp0_new);
										for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++, otherIt++) {
											const U32 ik0_new = P1C - 1 - ik1; // invert board
											*otherIt = pawnRow_new[ik0_new][ik1_new].load(std::memory_order_acquire);
										}
									}

								} else {
									if constexpr (P1C == 1) {
										std::unreachable();
									} else {
										const U32 bbp1_taken = bbp1 & ~landPiece;
										const int ip0_taken = ip0s_taken[std::popcount(bbp1 & (landPiece - 1))];
										const int ip1_taken = rankSecondPieces<true, P1C - 1, P0C>(bbp0_new, bbp1_taken);
										const auto& pawnrow_taken = std::get<rowIndex<TB_MEN>(P1C - 1, P0C)>(tb)[ip0_taken][ip1_taken];
										__builtin_prefetch(pawnrow_taken.data(), 0, 0);

										for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
											auto& rowK0 = rowP1[ik0];
											const U32 bbk0 = unrankFirstKing<false, P0C, P1C>(ik0, bbp0);
											const U32 bbk0_new = sourcePiece == bbk0 ? landPiece : bbk0;
											const U32 ik1_taken = rankSecondKing<true, P1C - 1, P0C>(bbk0_new, bbp0_new);
											for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++, otherIt++) {
												const U32 bbk1 = unrankSecondKing<false, P0C, P1C>(ik1, bbp1);
												if (landPiece == bbk1) { // King takes are obviously resolved
													*otherIt = 0;
													continue;
												}
												const U32 ik0_taken = P1C - 1 - ik1 - (landPiece > bbk1); // invert board, the taken piece shifts the king down if above it
												*otherIt = pawnrow_taken[ik0_taken][ik1_taken].load(std::memory_order_acquire);
											}
										}
									}
								}

								for (int i = 0; i < P0C * P1C; i++)
									unresolvedChildren[i] |= otherEntry[i] & cards.moveBoardsForward.sideCards[pp][std::countr_zero(landPiece)];
							}
						}
					}
				}



				// Unresolved card perms where every move leads to a resolved child, i.e. a win for the opponent.
				std::array<U32, P0C * P1C> newLostEntries;
				U32 lostUnion = 0;
				for (int i = 0; i < P0C * P1C; i++) {
					// Reload after the acquire loads of the children: a child that was seen resolved as a loss has
					// already marked its parents (this entry) before releasing, so those win bits must not count as lost.
					newLostEntries[i] = it[i].load(std::memory_order_acquire) & ~unmoveCardEntry(unresolvedChildren[i]);
					lostUnion |= newLostEntries[i];
				}
				if (!lostUnion)
					continue;

				{ // reverse movegen - all entries that can reach this entry are also marked as resolved.
					const U32 usedCards = usedCardsOfEntry(lostUnion);
					U32 sourcePieces = bbp1;
					for (int iSrc = 0; iSrc < P1C; iSrc++) {
						const U32 sourcePiece = sourcePieces & -sourcePieces;
						int pp = std::countr_zero(sourcePieces);
						sourcePieces &= sourcePieces - 1;
						const U32 bbp1_without_source = bbp1 - sourcePiece;
						U32 landPieces = landingsForCards(pp, usedCards, cards.moveBoardsForward) & ~(bbp0 | bbp1);
						while (landPieces) {
							const U32 landPiece = landPieces & -landPieces;
							landPieces &= landPieces - 1;
							const U32 bbp1_new = bbp1_without_source | landPiece;

							const int ip0_new = rankFirstPieces<true, P1C, P0C>(bbp1_new);
							const int ip1_new = rankSecondPieces<true, P1C, P0C>(bbp0, bbp1_new); // TODO incremental?
							auto& pawnRow_new = std::get<rowIndex<TB_MEN>(P1C, P0C)>(tb)[ip0_new][ip1_new];
							__builtin_prefetch(pawnRow_new.data(), 0, 0);

							auto& pawnRow_untaken = [&] -> auto& { // the <P1C, P0C + 1> row does not exist for the other rows
								if constexpr (P0C < TB_MEN / 2) {
									const U32 bbp0_untaken = bbp0 | sourcePiece;
									const int ip0_untaken = rankFirstPieces<true, P1C, P0C + 1>(bbp1_new);
									const int ip1_untaken = rankSecondPieces<true, P1C, P0C + 1>(bbp0_untaken, bbp1_new); // TODO incremental?
									auto& pawnRow_untaken = std::get<rowIndex<TB_MEN>(P1C, P0C + 1)>(tb)[ip0_untaken][ip1_untaken];
									__builtin_prefetch(pawnRow_untaken.data(), 0, 0);
									return pawnRow_untaken;
								} else
									return pawnRow_new;
							}();

							const U32 sideCards = cards.moveBoardsForward.sideCards[pp][std::countr_zero(landPiece)];
							auto lostIt = newLostEntries.begin();
							for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
								auto& rowK0 = rowP1[ik0];
								U32 bbk0;
								if constexpr (P0C < TB_MEN / 2)
									bbk0 = unrankFirstKing<false, P0C, P1C>(ik0, bbp0);
								const U32 ik1_new = P0C - 1 - ik0; // invert board
								for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++, lostIt++) {
									const U32 lostBits = *lostIt & sideCards;
									if (!lostBits)
										continue;
									const U32 newEntryBits = unmoveCardEntry(lostBits);
									const U32 bbk1 = unrankSecondKing<false, P0C, P1C>(ik1, bbp1);
									const U32 bbk1_new = sourcePiece == bbk1 ? landPiece : bbk1;
									const U32 ik0_new = rankFirstKing<true, P1C, P0C>(bbk1_new, bbp1_new);
									pawnRow_new[ik0_new][ik1_new].fetch_and(~newEntryBits, std::memory_order_relaxed);

									if constexpr (P0C < TB_MEN / 2) {
										const int ik1_untaken = ik1_new + (sourcePiece > bbk0); // the untaken piece shifts the king up if above it
										pawnRow_untaken[ik0_new][ik1_untaken].fetch_and(~newEntryBits, std::memory_order_relaxed);
									}
								}
							}
						}
					}
				}

				for (int i = 0; i < P0C * P1C; i++) {
					// Only after the reverse movegen, so a thread that sees these bits set also sees the parents marked.
					if (newLostEntries[i])
						it[i].fetch_and(~newLostEntries[i], std::memory_order_release);
				}
				updated = true;
			}
		}
	}
	rowStartChunk += P0_CHUNKS;
}

template <U16 TB_MEN, int STEP, typename Storage>
void singleDepthPass(const CardsInfo& cards, Storage& tb, std::atomic<U64>& chunkCounter, bool& updated) {
	U64 chunk = chunkCounter++;
	U64 rowStartChunk = 0;

	TableBase<TB_MEN>::forEachRow([&]<U16 P0C, U16 P1C> {
		processRow<TB_MEN, P0C, P1C, STEP>(cards, tb, chunk, rowStartChunk, chunkCounter, updated);
	});
}

template <typename Storage>
U64 countUnresolved(const Storage& tb) {
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

template <U16 TB_MEN>
consteval U64 countTotal() {
	U64 total = 0;
	TableBase<TB_MEN>::forEachRow([&]<U16 P0C, U16 P1C> {
		total += 30ULL * PAWNTABLE_P0<P0C, P1C>.size() * PAWNTABLE_P1<P0C, P1C>.size() * P0C * P1C;
	});
	return total;
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
	constexpr U64 total = countTotal<TB_MEN>();
	while (comm.updated && comm.iteration <= stopAtIteration) {
		chunkCounter = 0;
		comm.updated = false;
		const auto iterationStart = std::chrono::steady_clock::now();
		comm.sync.masterNotify(numThreads);
		comm.sync.masterWait(numThreads);
		const auto countingStart = std::chrono::steady_clock::now();
		const std::chrono::duration<double> iterationTime = countingStart - iterationStart;

		if constexpr (VERBOSE) {
			const U64 count = total - countUnresolved(tb);
			newResolvedStates = count - resolvedStates;
			resolvedStates = count;
			if (iterationTime.count() > .02 || newResolvedStates == 0)
				std::cout << std::format("it {:3}: {:12} ({:.4f}%) in {:.2f} seconds\n", comm.iteration, newResolvedStates, 100.0 * resolvedStates / total, iterationTime.count());
		} else
			std::cout << "." << std::flush;

		countingTime += std::chrono::steady_clock::now() - countingStart;
		comm.iteration++;
	}

	const std::chrono::duration<double> totalTime = std::chrono::steady_clock::now() - startTime - countingTime;
	std::cout << std::format("\ntotal {}-men: {} states in {:.2f} seconds (+{:.2f} seconds counting)\n", TB_MEN, resolvedStates, totalTime.count(), countingTime.count());

	if constexpr (!VERBOSE)
		resolvedStates = total - countUnresolved(tb);
	if (resolvedStates != EXPECTED_RESOLVED_STATES) {
		std::cerr << "ERROR: WRONG NUMBER OF RESOLVED BOARDS (got " << resolvedStates << ", expected " << EXPECTED_RESOLVED_STATES << ")\n";
		throw std::runtime_error("wrong number of boards");
	}

	comm.iteration = 0;
	comm.sync.masterNotify(numThreads);
	for (auto& thread : threads)
		thread.join();
}

export template <U16 TB_MEN>
struct TableBase {
	using CardsEntry = std::atomic<U32>;

	template <U16 P0C, U16 P1C>
	using TableKingPermsP1 = std::array<CardsEntry, P1C>;

	template <U16 P0C, U16 P1C>
	using TableKingPermsP0 = std::array<TableKingPermsP1<P0C, P1C>, P0C>;

	template <U16 P0C, U16 P1C>
	using TableP1 = std::array<TableKingPermsP0<P0C, P1C>, PAWNTABLE_P1<P0C, P1C>.size()>;

	template <U16 P0C, U16 P1C>
	struct alignas(64) TableRow : std::array<TableP1<P0C, P1C>, PAWNTABLE_P0<P0C, P1C>.size()> {};

	template <std::size_t... I>
	static auto tableBaseStorage(std::index_sequence<I...>) -> std::tuple<TableRow<PIECE_COUNTS<TB_MEN>[I].p0c, PIECE_COUNTS<TB_MEN>[I].p1c>...>;

	using TableBaseStorage = decltype(tableBaseStorage(std::make_index_sequence<PIECE_COUNTS<TB_MEN>.size()>{}));

	static constexpr void forEachRow(auto&& f) {
		[&]<std::size_t... I>(std::index_sequence<I...>) {
			(f.template operator()<PIECE_COUNTS<TB_MEN>[I].p0c, PIECE_COUNTS<TB_MEN>[I].p1c>(), ...);
		}(std::make_index_sequence<PIECE_COUNTS<TB_MEN>.size()>{});
	}

	explicit TableBase(const CardsInfo& cards, U64 stopAtIteration = std::numeric_limits<U64>::max()) {
		const auto allocStart = std::chrono::steady_clock::now();
		// make_unique makes clang shit itself at compile time
		void* storage = ::operator new(sizeof(TableBaseStorage), std::align_val_t{alignof(TableBaseStorage)});
		std::fill_n(static_cast<U32*>(storage), sizeof(TableBaseStorage) / sizeof(U32), CARD_PERMS_MASK);
		tb.reset(static_cast<TableBaseStorage*>(storage));

		const std::chrono::duration<double> allocTime = std::chrono::steady_clock::now() - allocStart;
		std::cout << std::format("allocated {:.1f}GB in {:.2f} seconds\n", sizeof(TableBaseStorage) / 1e9, allocTime.count());
		runTableBaseBuild<TB_MEN>(cards, *tb, stopAtIteration, allocStart);
	}

	std::unique_ptr<TableBaseStorage> tb;
};
