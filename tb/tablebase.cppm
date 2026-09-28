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


// STEP 1: no TB lookups, just check win in 0/invalid boards & win in 1.
// STEP 2: TB lookups.
template <U16 TB_MEN, U32 ROW, int STEP>
void processRow(const CardsInfo& cards, auto& tb, U64& chunk, U64& rowStartChunk, std::atomic<U64>& chunkCounter, U64& newStates) {
	auto& row = std::get<ROW>(tb.tb);
	constexpr U64 P0_CHUNKS = (row.size() - 1) / CHUNK_P0_POSITIONS + 1;

	for (; chunk < rowStartChunk + P0_CHUNKS; chunk = chunkCounter++) {
		const U32 begin = static_cast<U32>((chunk - rowStartChunk) * CHUNK_P0_POSITIONS);
		const U32 end = std::min<U32>(begin + CHUNK_P0_POSITIONS, static_cast<U32>(row.size()));
		for (int ip0 = begin; ip0 < end; ip0++) {
			auto& rowP0 = row[ip0];
			const U32 bbp1 = unrankFirstPieces<TB_MEN, ROW, true>(ip0);
			for (int ip1 = 0; ip1 < static_cast<int>(rowP0.size()); ip1++) {
				auto& rowP1 = rowP0[ip1];
				const U32 bbp0 = unrankSecondPieces<TB_MEN, ROW, true>(ip1, bbp1);
				const int ip0_new = rankFirstPieces<TB_MEN, ROW, false>(bbp0);
				for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
					auto& rowK0 = rowP1[ik0];
					const U32 bbk1 = unrankFirstKing<TB_MEN, ROW, true>(ik0, bbp1);
					for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++) {
						auto& cardsEntry = rowK0[ik1];
						const U32 bbk0 = unrankSecondKing<TB_MEN, ROW, true>(ik1, bbp0);
						const int ik0_new = rankFirstKing<TB_MEN, ROW, false>(bbp0, ik0);

						U32 entry;
						if constexpr (STEP == 1) {
							entry = (1U << 30) - 1;
							Board board{ bbp0, bbp1, bbk0, bbk1 };
							if (board.isTempleEnded()) { // Win in 0
								cardsEntry.store(0, std::memory_order_relaxed);
								continue;
							}
							entry &= ~board.getWinInOneCards<0>(cards.moveBoardsReverse);
							cardsEntry.store(entry, std::memory_order_relaxed);

						} else {
							// loop over all entries, when a bit is 1 that means the entry is still unresolved.
							if (!(entry = cardsEntry.load(std::memory_order_relaxed)))
								continue;
						}
						U32 newEntries = 0;


						{ // forwards movegen - check if all possible p1 moves are resolved
							U32 sourcePieces = bbp1 & ~bbk0; // No need to check king takes ;)
							for (int iSrc = 0; iSrc < PIECE_COUNTS<TB_MEN>[ROW].p1c; iSrc++) {
								const U32 sourcePiece = sourcePieces & -sourcePieces;
								int pp = std::countr_zero(sourcePieces);
								sourcePieces &= sourcePieces - 1;
								const U32 bbp1_without_source = bbp1 - sourcePiece;
								U32 landPieces = cards.moveBoardsReverse.all[pp];
								while (landPieces) {
									const U32 landPiece = landPieces & -landPieces;
									landPieces &= landPieces - 1;
									const U32 bbp1_new = bbp1_without_source | landPiece;
									const U32 bbk1_new = sourcePiece == bbk1 ? landPiece : bbk1;

									if constexpr (STEP == 1) {
										Board board{
											.bbp = { bbp0, bbp1_new },
											.bbk = { bbk0, bbk1_new },
										};
										newEntries |= ~board.getWinInOneCards<1>(cards.moveBoardsForward);
									} else {

										int ip0_new2 = ip0_new;
										if (landPiece & bbp0) // takes move
											ip0_new2 = rankFirstPieces<TB_MEN, ROW, false>(bbp0 & ~landPiece);

										const int ip1_new = rankSecondPieces<TB_MEN, ROW, false>(bbp1_new, bbp0 & ~landPiece);
										const int ik1_new = rankSecondKing<TB_MEN, ROW, false>(bbk1_new, bbp1_new);

										{
											auto& rowStorage = std::get<ROW>(tb.tb);
											const U32 otherEntry = rowStorage[ip0_new2][ip1_new][ik0_new][ik1_new].load(std::memory_order_relaxed);
											newEntries |= p1_use_card0_unmasked(~otherEntry) | P1_CARD0_USED_IN_MOVE_MASK[pp]; // TODO: mask pre scrambling?
											newEntries |= p1_use_card1_unmasked(~otherEntry) | P1_CARD1_USED_IN_MOVE_MASK[pp];
										}
									}
								}
							}
						}

						newEntries &= entry;
						if (entry == newEntries) // all unresolved entries survived, nothing to update
							continue;

						cardsEntry.fetch_and(newEntries, std::memory_order_relaxed); // mark this entry as resolved

						{ // reverse movegen - all entries that can reach this entry are also marked as resolved.
							U32 sourcePieces = bbp0;
							for (int iSrc = 0; iSrc < PIECE_COUNTS<TB_MEN>[ROW].p0c; iSrc++) {
								const U32 sourcePiece = sourcePieces & -sourcePieces;
								int pp = std::countr_zero(sourcePieces);
								sourcePieces &= sourcePieces - 1;
								const U32 bbp0_without_source = bbp0 - sourcePiece;
								U32 landPieces = cards.moveBoardsReverse.all[pp];
								while (landPieces) {
									const U32 landPiece = landPieces & -landPieces;
									landPieces &= landPieces - 1;
									const U32 bbp0_new = bbp0_without_source | landPiece;
									const U32 bbk0_new = sourcePiece == bbk0 ? landPiece : bbk0;

									const int ip0_new = rankFirstPieces<TB_MEN, ROW, false>(bbp0_new);
									const int ip1_new = rankSecondPieces<TB_MEN, ROW, false>(bbp1, bbp0_new); // TODO incremental?
									const int ik0_new = rankFirstKing<TB_MEN, ROW, false>(bbk0_new, bbp0_new);
									const int ik1_new = rankSecondKing<TB_MEN, ROW, false>(bbk1, bbp1);

									const U32 newEntryBits = (p0_use_card0_unmasked(newEntries) | P0_CARD0_USED_IN_MOVE_MASK[pp]) & (p0_use_card1_unmasked(newEntries) | P0_CARD1_USED_IN_MOVE_MASK[pp]);

									std::get<ROW>(tb.tb)[ip0_new][ip1_new][ik0_new][ik1_new].fetch_and(newEntryBits, std::memory_order_relaxed);

									const int ip1_new_takes = rankSecondPieces<TB_MEN, ROW, false>(bbp1 | sourcePiece, bbp0_new); // TODO incremental?
									const int ik1_new_takes = rankSecondKing<TB_MEN, ROW, false>(bbk1, bbp1 | sourcePiece);
									std::get<ROW>(tb.tb)[ip0_new][ip1_new_takes][ik0_new][ik1_new_takes].fetch_and(newEntryBits, std::memory_order_relaxed);
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
void singleDepthPass(const CardsInfo& cards, Table& tb, std::atomic<U64>& chunkCounter, std::atomic<U64>& stateCounter) {
	U64 newStates = 0;
	U64 chunk = chunkCounter++;
	U64 rowStartChunk = 0;

	[&]<U32... ROW>(std::integer_sequence<U32, ROW...>) {
		(processRow<TB_MEN, ROW, STEP>(cards, tb, chunk, rowStartChunk, chunkCounter, newStates), ...);
	}(std::make_integer_sequence<U32, std::tuple_size_v<typename Table::TableBaseStorage>>{});

	stateCounter += newStates;
}

template <U16 TB_MEN, typename Table>
void singleThread(const CardsInfo& cards, Table& tb, std::atomic<U64>& chunkCounter, std::atomic<U64>& stateCounter, ThreadObj& comm) {
	while (true) {
		comm.sync.slaveNotifyWait();
		if (comm.depth == 0)
			break;

		if (comm.depth == 2)
			singleDepthPass<TB_MEN, 1>(cards, tb, chunkCounter, stateCounter);
		else
			singleDepthPass<TB_MEN, 2>(cards, tb, chunkCounter, stateCounter);
	}
}

template <U16 TB_MEN, typename Table>
void runTableBaseBuild(const CardsInfo& cards, Table& table) {
	std::atomic<U64> chunkCounter;
	std::atomic<U64> stateCounter = 0;
	ThreadObj comm;
	int numThreads = std::clamp<int>(static_cast<int>(std::thread::hardware_concurrency()), 1, 1024);
	std::vector<std::thread> threads(numThreads);
	for (int i = 0; i < numThreads; i++)
		threads[i] = std::thread(singleThread<TB_MEN, Table>, std::cref(cards), std::ref(table), std::ref(chunkCounter), std::ref(stateCounter), std::ref(comm));
	comm.sync.masterWait(numThreads);

	U64 lastStateCounter = 1;
	while (stateCounter != lastStateCounter) {
		chunkCounter = 0;
		lastStateCounter = stateCounter;
		comm.sync.masterNotify(numThreads);
		comm.sync.masterWait(numThreads);
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

	explicit TableBase(const CardsInfo& cards) {
		tablebase_impl::runTableBaseBuild<TB_MEN>(cards, *this);
	}

	TableBaseStorage tb;
};
