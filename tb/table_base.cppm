export module tb:table_base;
import std;
import :types;
import :card;
import :index;
import :sync;

namespace table_base_impl {

struct ThreadObj {
	Sync sync;
	U64 depth = 2;
};

constexpr U64 CHUNK_P0_POSITIONS = 16;

template <U16 TB_MEN, U32 ROW, int DEPTH>
void processRow(const CardsInfo& cards, auto& tb, U64& chunk, U64& rowStartChunk, std::atomic<U64>& chunkCounter, U64& newStates) {
	auto& row = std::get<ROW>(tb.tb);
	constexpr U64 P0_CHUNKS = (row.size() - 1) / CHUNK_P0_POSITIONS + 1;

	for (; chunk < rowStartChunk + P0_CHUNKS; chunk = chunkCounter++) {
		const U32 begin = static_cast<U32>((chunk - rowStartChunk) * CHUNK_P0_POSITIONS);
		const U32 end = std::min<U32>(begin + CHUNK_P0_POSITIONS, static_cast<U32>(row.size()));
		for (int ip0 = begin; ip0 < end; ip0++) {
			const auto& rowP0 = row[ip0];
			const U32 bbp1 = unrankFirstPieces<TB_MEN, ROW, true>(ip0);
			for (int ip1 = 0; ip1 < static_cast<int>(rowP0.size()); ip1++) {
				const auto& rowP1 = rowP0[ip1];
				const U32 bbp0 = unrankSecondPieces<TB_MEN, ROW, true>(ip1, bbp1);
				const int ip0_new = rankFirstPieces<TB_MEN, ROW, false>(bbp0);
				for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
					const auto& rowK0 = rowP1[ik0];
					const U32 bbk1 = unrankFirstKing<TB_MEN, ROW, true>(ik0, bbp1);
					for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++) {
						const auto& cardsEntry = rowK0[ik1];
						const U32 bbk0 = unrankSecondKing<TB_MEN, ROW, true>(ik1, bbp0);
						const int ik0_new = rankFirstKing<TB_MEN, ROW, false>(bbp0, ik0);

						U32 entry = cardsEntry.load(std::memory_order_relaxed);
						if (!entry)
							continue;

						U32 sourcePieces = bbp1;
						for (int iSrc = 0; iSrc < PIECE_COUNTS<TB_MEN>[ROW].p1c; iSrc++) {
							const U32 sourcePiece = sourcePieces & -sourcePieces;
							int pp = std::countr_zero(sourcePieces);
							sourcePieces &= sourcePieces - 1;
							const U32 bbp1_without_source = bbp1 - sourcePiece;
							U32 landPieces = cards.moveBoardsForward.all[pp];
							while (landPieces) {
								const U32 landPiece = landPieces & -landPieces;
								landPieces &= landPieces - 1;
								const U32 bbp1_new = bbp1_without_source | landPiece;
								const U32 bbk1_new = sourcePiece == bbk1 ? landPiece : bbk1;

								const U32 ip1_new = rankSecondPieces<TB_MEN, ROW, false>(bbp1_new, bbp0);
								const U32 ik1_new = rankSecondKing<TB_MEN, ROW, false>(bbk1_new, bbp1_new);

								{
									auto& rowStorage = std::get<ROW>(tb.tb);
									const auto& otherEntry = rowStorage[ip0_new][ip1_new][ik0_new][ik1_new];
									entry &= unuse_card0_unmasked(otherEntry) | ~CARD0_USED_IN_MOVE[pp];
									entry &= unuse_card1_unmasked(otherEntry) | ~CARD1_USED_IN_MOVE[pp];
								}

								if constexpr (PIECE_COUNTS<TB_MEN>[ROW].p0c < TB_MEN / 2) {
									const U32 bbp0_takes = bbp0 | sourcePiece;
									const U32 ip0_new_takes = rankFirstPieces<TB_MEN, ROW, false>(bbp0_takes);
									const U32 ip1_new_takes = rankSecondPieces<TB_MEN, ROW, false>(bbp1_new, bbp0_takes);
									const U32 ik0_new_takes = rankFirstKing<TB_MEN, ROW, false>(bbk0, bbp0_takes);
									auto& rowStorage = std::get<ROW>(tb.tb);
									const auto& otherEntry = rowStorage[ip0_new_takes][ip1_new_takes][ik0_new_takes][ik1_new];
									entry &= unuse_card0_unmasked(otherEntry) | ~CARD0_USED_IN_MOVE[pp];
									entry &= unuse_card1_unmasked(otherEntry) | ~CARD1_USED_IN_MOVE[pp];
								}
							}
						}

						if (!entry)
							continue;
					}
				}
			}
		}
	}
	rowStartChunk += P0_CHUNKS;
}

template <U16 TB_MEN, int DEPTH, typename Table>
void singleDepthPass(const CardsInfo& cards, Table& tb, std::atomic<U64>& chunkCounter, std::atomic<U64>& stateCounter) {
	U64 newStates = 0;
	U64 chunk = chunkCounter++;
	U64 rowStartChunk = 0;

	[&]<U32... ROW>(std::integer_sequence<U32, ROW...>) {
		(processRow<TB_MEN, ROW, DEPTH>(cards, tb, chunk, rowStartChunk, chunkCounter, newStates), ...);
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
			singleDepthPass<TB_MEN, 2>(cards, tb, chunkCounter, stateCounter);
		else
			singleDepthPass<TB_MEN, 3>(cards, tb, chunkCounter, stateCounter);
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

} // namespace table_base_impl

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
		table_base_impl::runTableBaseBuild<TB_MEN>(cards, *this);
	}

	TableBaseStorage tb;
};
