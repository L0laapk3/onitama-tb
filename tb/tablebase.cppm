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
	U64 depth = 2;
};

constexpr U64 CHUNK_P0_POSITIONS = 16;


template <bool atomic = 0>
struct Stats {
	std::conditional_t<atomic, std::atomic<U64>, U64> resolvedStates = 0;
	std::conditional_t<atomic, std::atomic<U64>, U64> WinIn0 = 0;
	std::conditional_t<atomic, std::atomic<U64>, U64> WinIn1 = 0;
	std::conditional_t<atomic, std::atomic<U64>, U64> WinIn2 = 0;

	template <typename Other>
	Stats& operator+=(const Other& other) {
		resolvedStates += other.resolvedStates;
		WinIn0 += other.WinIn0;
		WinIn1 += other.WinIn1;
		WinIn2 += other.WinIn2;
		return *this;
	}
};

// STEP 1: no TB lookups, just check win in 0/invalid boards & win in 1.
// STEP 2: TB lookups.
template <U16 TB_MEN, U32 ROW, int STEP>
void processRow(const CardsInfo& cards, auto& tb, U64& chunk, U64& rowStartChunk, std::atomic<U64>& chunkCounter, Stats<>& stats) {
	auto& row = std::get<ROW>(tb.tb);
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
							entry = (1U << 30) - 1;
							Board board{ bbp0, bbp1, bbk0, bbk1 };
							Board inverseBoard{ bbp0_inv, bbp1_inv, bbk0_inv, bbk1_inv };
							if (board.isTempleEnded() || inverseBoard.isTempleEnded()) { // Win in 0
								cardsEntry.store(0, std::memory_order_relaxed);
								stats.WinIn0 += 30;
								stats.resolvedStates += 30;
								continue;
							}
							entry &= ~inverseBoard.getWinInOneCards<0>(cards.moveBoardsReverse);
							cardsEntry.store(entry, std::memory_order_relaxed);
							const U32 winInOne = 30 - std::popcount(entry);
							stats.WinIn1 += winInOne;
							stats.resolvedStates += winInOne;

						} else {
							// loop over all entries, when a bit is 1 that means the entry is still unresolved.
							if (!(entry = cardsEntry.load(std::memory_order_relaxed)))
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
										U32 otherEntry;
										if (!isTakeMove) {
											const int ip1_new = rankSecondPieces<TB_MEN, MIRROR_ROW, false>(bbp1_new, bbp0);
											const int ik1_new = rankSecondKing<TB_MEN, MIRROR_ROW, false>(bbk1_new, bbp1_new);
											otherEntry = std::get<MIRROR_ROW>(tb.tb)[ip0_new][ip1_new][ik0_new][ik1_new].load(std::memory_order_relaxed);
										} else {
											if constexpr (PC.p1c == 1) {
												otherEntry = 0; // when p0 only has its king left, every take is a king take
											} else {
												const U32 bbp0_taken = bbp0 & ~landPiece;
												const int ip0_taken = rankFirstPieces<TB_MEN, P0_TAKEN_ROW, false>(bbp0_taken);
												const int ip1_taken = rankSecondPieces<TB_MEN, P0_TAKEN_ROW, false>(bbp1_new, bbp0_taken);
												const int ik0_taken = rankFirstKing<TB_MEN, P0_TAKEN_ROW, false>(bbk0, bbp0_taken);
												const int ik1_taken = rankSecondKing<TB_MEN, P0_TAKEN_ROW, false>(bbk1_new, bbp1_new);
												otherEntry = std::get<P0_TAKEN_ROW>(tb.tb)[ip0_taken][ip1_taken][ik0_taken][ik1_taken].load(std::memory_order_relaxed);
											}
										}

										unresolvedAfterMove |= otherEntry & cards.moveBoardsReverse.sideCards[pp][std::countr_zero(landPiece)];
									}
								}
							}
						}
						newEntries |= unmoveCardEntry(unresolvedAfterMove);

						newEntries &= entry;
						if (entry == newEntries) // all unresolved entries survived, nothing to update
							continue;

						// Count only bits this thread actually clears. Another thread may have
						// changed the entry since it was loaded above.
						const U32 previousEntry = cardsEntry.fetch_and(newEntries, std::memory_order_relaxed);
						const U32 lost = previousEntry & ~newEntries;
						const U32 resolved = std::popcount(lost);
						stats.resolvedStates += resolved;
						if constexpr (STEP == 1)
							stats.WinIn2 += resolved;

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

									const U32 newEntryBits = ~unmoveCardEntry(lost & cards.moveBoardsReverse.sideCards[pp][std::countr_zero(landPiece)]);

									std::get<MIRROR_ROW>(tb.tb)[ip0_new][ip1_new][ik0_new][ik1_new].fetch_and(newEntryBits, std::memory_order_relaxed);

									if constexpr (P1_UNTAKEN_ROW >= 0) {
										const U32 bbp1_untaken = bbp1 | sourcePiece;
										const int ip0_untaken = rankFirstPieces<TB_MEN, P1_UNTAKEN_ROW, false>(bbp0_new);
										const int ip1_untaken = rankSecondPieces<TB_MEN, P1_UNTAKEN_ROW, false>(bbp1_untaken, bbp0_new); // TODO incremental?
										const int ik0_untaken = rankFirstKing<TB_MEN, P1_UNTAKEN_ROW, false>(bbk0_new, bbp0_new);
										const int ik1_untaken = rankSecondKing<TB_MEN, P1_UNTAKEN_ROW, false>(bbk1, bbp1_untaken);
										std::get<P1_UNTAKEN_ROW>(tb.tb)[ip0_untaken][ip1_untaken][ik0_untaken][ik1_untaken].fetch_and(newEntryBits, std::memory_order_relaxed);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	rowStartChunk += P0_CHUNKS;
}

template <U16 TB_MEN, int STEP, typename Table>
void singleDepthPass(const CardsInfo& cards, Table& tb, std::atomic<U64>& chunkCounter, Stats<1>& globalStats) {
	Stats stats;
	U64 chunk = chunkCounter++;
	U64 rowStartChunk = 0;

	[&]<U32... ROW>(std::integer_sequence<U32, ROW...>) {
		(processRow<TB_MEN, ROW, STEP>(cards, tb, chunk, rowStartChunk, chunkCounter, stats), ...);
	}(std::make_integer_sequence<U32, std::tuple_size_v<typename Table::TableBaseStorage>>{});

	globalStats += stats;
}

template <U16 TB_MEN, typename Table>
void singleThread(const CardsInfo& cards, Table& tb, std::atomic<U64>& chunkCounter, Stats<1>& globalStats, ThreadObj& comm) {
	while (true) {
		comm.sync.slaveNotifyWait();
		if (comm.depth == 0)
			break;

		if (comm.depth == 2)
			singleDepthPass<TB_MEN, 1>(cards, tb, chunkCounter, globalStats);
		else
			singleDepthPass<TB_MEN, 2>(cards, tb, chunkCounter, globalStats);
	}
}

template <U16 TB_MEN, typename Table>
void runTableBaseBuild(const CardsInfo& cards, Table& table, U64 stopAtDepth) {
	constexpr U64 EXPECTED_WIN_IN_ONE = 537541377ULL;
	// 30 card perms. 47 perms with kings on their temple. times all combinations of zero to 2 pawns on each side
	constexpr U64 EXPECTED_WIN_IN_ZERO = 30 * 47 * (1 + 23 + 23*22/2 + 23 * (1 + 22 + 22*21/2) + 23*22/2 * (1 + 21 + 21*20/2));
	constexpr U64 EXPECTED_RESOLVED_STATES = EXPECTED_WIN_IN_ZERO + EXPECTED_WIN_IN_ONE + (TB_MEN == 6 ? 537649967ULL : 19974501547ULL);

	std::atomic<U64> chunkCounter;
	Stats<1> globalStats{};
	ThreadObj comm;
	int numThreads = std::clamp<int>(static_cast<int>(std::thread::hardware_concurrency()), 1, 1024);
	std::vector<std::thread> threads(numThreads);
	for (int i = 0; i < numThreads; i++)
		threads[i] = std::thread(singleThread<TB_MEN, Table>, std::cref(cards), std::ref(table), std::ref(chunkCounter), std::ref(globalStats), std::ref(comm));
	comm.sync.masterWait(numThreads);

	U64 newResolvedStates = 1;
	while (newResolvedStates && comm.depth <= stopAtDepth) {
		chunkCounter = 0;
		U64 lastStateCounter = globalStats.resolvedStates;
		comm.sync.masterNotify(numThreads);
		comm.sync.masterWait(numThreads);
		newResolvedStates = globalStats.resolvedStates - lastStateCounter;
		if (comm.depth == 2) {
			std::cout << "Distance    0: " << globalStats.WinIn0 << "\n";
			std::cout << "Distance    1: " << globalStats.WinIn1 << "\n";
			std::cout << "Distance    2: " << globalStats.WinIn2 << "\n";
			if (globalStats.resolvedStates != globalStats.WinIn0 + globalStats.WinIn1 + globalStats.WinIn2) {
				std::cerr << "ERROR: STEP 1 BOOKKEEPING INCONSISTENCY (got " << globalStats.resolvedStates << ", expected " << globalStats.WinIn0 + globalStats.WinIn1 + globalStats.WinIn2 << ")\n";
				throw std::runtime_error("step 1 bookkeeping inconsistency");
			}
		} else
			std::cout << std::format("Iteration {:3}: {}\n", comm.depth, newResolvedStates);

		if (comm.depth == 2) {
			if constexpr (TB_MEN == 6) {
				if (globalStats.WinIn0 != EXPECTED_WIN_IN_ZERO) {
					std::cerr << "ERROR: WRONG NUMBER OF WIN-IN-0 BOARDS (got " << globalStats.WinIn0 << ", expected " << EXPECTED_WIN_IN_ZERO << ")\n";
					throw std::runtime_error("wrong number of win-in-0 boards");
				}

				if (globalStats.WinIn1 != EXPECTED_WIN_IN_ONE) {
					std::cerr << "ERROR: WRONG NUMBER OF WIN-IN-1 BOARDS (got " << globalStats.WinIn1 << ", expected " << EXPECTED_WIN_IN_ONE << ")\n";
					throw std::runtime_error("wrong number of win-in-1 boards");
				}
			}
		}

		comm.depth++;
	}

	if constexpr (TB_MEN == 6 || TB_MEN == 8) {
		constexpr U64 EXPECTED_RESOLVED_STATES = EXPECTED_WIN_IN_ZERO + EXPECTED_WIN_IN_ONE + (TB_MEN == 6 ? 537649967ULL : 19974501547ULL);
		if (!newResolvedStates && globalStats.resolvedStates != EXPECTED_RESOLVED_STATES) {
			std::cerr << "ERROR: WRONG NUMBER OF RESOLVED BOARDS (got " << globalStats.resolvedStates << ", expected " << EXPECTED_RESOLVED_STATES << ")\n";
			throw std::runtime_error("wrong number of boards");
		}
	}

	comm.depth = 0;
	comm.sync.masterNotify(numThreads);
	for (auto& thread : threads)
		thread.join();
}

} // namespace tablebase_impl

export template <U16 TB_MEN>
struct TableBase {
	struct CardsEntry : std::atomic<U32> {
		CardsEntry() : std::atomic<U32>((1U << 30) - 1) {}
	};

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

	explicit TableBase(const CardsInfo& cards, U64 stopAtDepth = std::numeric_limits<U64>::max()) {
		tablebase_impl::runTableBaseBuild<TB_MEN>(cards, *this, stopAtDepth);
	}

	TableBaseStorage tb;
};
