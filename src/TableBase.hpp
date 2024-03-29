

#include "Board.h"
#include "Index.hpp"
#include "Card.hpp"
#include "Sync.h"

#include <vector>
#include <atomic>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iostream>
#include <cassert>
#include <immintrin.h>
#include <x86intrin.h>

#define NO_PRINTS


template <U16 TB_MEN>
constexpr U64 CHUNK_SIZE = 1; // a divisor of PIECECOUNTMULT * KINGSMULT is used

struct ThreadObj {
	Sync sync;
	U64 depth = 2;
	U8 cardI = 0;
};

template<U16 TB_MEN, bool STORE_WIN, int depth>
void singleDepthPass(const CardsInfo& cards, U8 cardI, TableBase<TB_MEN, STORE_WIN>& tb, std::atomic<U64>& chunkCounter, bool& modified);
template<U16 TB_MEN, bool STORE_WIN>
void singleThread(const CardsInfo& cards, TableBase<TB_MEN, STORE_WIN>& tb, std::atomic<U64>& chunkCounter, bool& modified, ThreadObj& comm, U64 threadI) {

	while (true) {

		comm.sync.slaveNotifyWait();
		if (comm.depth == 0)
			break;

		tb.determineUnloads(comm.cardI, [&](RefRowWrapper<TB_MEN, STORE_WIN>& row) {
			if (row.isChanged || !row.isCompressed)
				row.partialCompress(threadI);
			comm.sync.slaveNotifyWait();
		});
		U8 invCardI = CARDS_INVERT[comm.cardI];
		for (U8 decompressCardI : std::array<U8, 5>{
			comm.cardI,
			CARDS_SWAP[invCardI][1][0],
			CARDS_SWAP[invCardI][1][1],
			CARDS_SWAP[invCardI][0][0],
			CARDS_SWAP[invCardI][0][1],
		}) {
			auto& row = tb[decompressCardI];
			if (!row.isDecompressed) {
				row.partialDecompress(threadI);
				comm.sync.slaveNotifyWait();
			}
		}

		if (comm.depth == 2)
			singleDepthPass<TB_MEN, STORE_WIN, 2>(cards, comm.cardI, tb, chunkCounter, modified);
		else
			singleDepthPass<TB_MEN, STORE_WIN, 3>(cards, comm.cardI, tb, chunkCounter, modified);
	}
}

template<U16 TB_MEN, bool STORE_WIN>
std::unique_ptr<TableBase<TB_MEN, STORE_WIN>> TableBase<TB_MEN, STORE_WIN>::generate(const CardsInfo& cards, U64 memory_allowance) {


	U64 totalLoads = 0;
	U64 totalDecompressions = 0;

	int numThreads = std::clamp<int>(std::thread::hardware_concurrency(), 1, 1024);

	auto tb = std::make_unique<TableBase<TB_MEN, STORE_WIN>>();

	tb->memory_remaining = memory_allowance;

	auto a = sizeof(typename RefRowWrapper<TB_MEN, STORE_WIN>::RefRow);

	std::cout << "jump table size: " << sizeof(typename RefRowWrapper<TB_MEN, STORE_WIN>::RefRow) / sizeof(U32) * CARDSMULT << " entries (" << sizeof(typename RefRowWrapper<TB_MEN, STORE_WIN>::RefRow) * CARDSMULT / 1024 << "KiB)" << std::endl;

	U64 totalRows = 0, totalSize = 0;
	tb->cnt_0 = 0;
	for (U8 i = 0; i < CARDSMULT; i++) {
		U8 cardI = UNLOAD_ORDER[0][i];
		RefRowWrapper<TB_MEN, STORE_WIN>& cardTb = (*tb)[cardI];
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard combinedOtherMoveBoard = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[0][0]], cards.moveBoardsForward[permutation.playerCards[0][1]]);


		U64 rows = 0, size = 0;
		iterateTBCounts<TB_MEN>(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
			rows += (rowSize + NUM_BOARDS_PER_ENTRY<STORE_WIN> - 1) / NUM_BOARDS_PER_ENTRY<STORE_WIN>;
			size += rowSize;
		});

		totalRows += rows;
		totalSize += size;


		tb->determineUnloads(cardI, [&](RefRowWrapper<TB_MEN, STORE_WIN>& row) {
			row.initiateCompress(*tb);
			for (int j = 0; j < numThreads; j++) // TODO: thread
				row.partialCompress(j);
			row.finishCompress(*tb);
		});
		cardTb.memComp = MemCompVec(numThreads);
		cardTb.mem = MemVec(rows);
		for (int i = 0; i < rows; i++)
			cardTb.mem[i] = 0;
		tb->memory_remaining -= rows * sizeof(TB_ENTRY);

		U32 passedRowsCount = 0;
		// set final bits to resolved.
		iterateTBCounts<TB_MEN>(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
			cardTb.refs[pieceCnt_kingsIndex] = passedRowsCount;
			if (rowSize == 0)
				return;
			U64 rowEntries = (rowSize + NUM_BOARDS_PER_ENTRY<STORE_WIN> - 1) / NUM_BOARDS_PER_ENTRY<STORE_WIN>;
			passedRowsCount += rowEntries;
			cardTb.mem[passedRowsCount - 1] = (NUM_BOARDS_PER_ENTRY<STORE_WIN> < sizeof(TB_ENTRY) * 8 ? 1ULL << NUM_BOARDS_PER_ENTRY<STORE_WIN> : 0) - (1ULL << (((rowSize + NUM_BOARDS_PER_ENTRY<STORE_WIN> - 1) % NUM_BOARDS_PER_ENTRY<STORE_WIN>) + 1)); // mark final rows as resolved so we dont have to worry about it
			tb->cnt_0 -= countResolved<STORE_WIN>(cardTb.mem[passedRowsCount - 1]);
		});

		cardTb.isDecompressed = true;
		cardTb.refs.back() = passedRowsCount;
	}

	std::cout << "main table size: " << totalSize << " entries (" << totalRows * sizeof(TB_ENTRY) / 1024 / 1024 << "MiB)" << std::endl;



	U64 totalBoards = 0;
	float totalTime = 0;
	auto beginLoopTime = std::chrono::steady_clock::now();
	auto beginTime = beginLoopTime;
	bool modified = false;
	int lastModified = 0;
	bool keepCompressed = false;
	std::atomic<U64> chunkCounter = 0;
	ThreadObj comm;

	std::vector<std::thread> threads(numThreads);
	for (int i = 0; i < numThreads; i++)
		threads[i] = std::thread(&singleThread<TB_MEN, STORE_WIN>, std::cref(cards), std::ref(*tb), std::ref(chunkCounter), std::ref(modified), std::ref(comm), i);

	comm.sync.masterWait(numThreads);

	while (true) {

		std::vector<RefRowWrapper<TB_MEN, STORE_WIN>*> compressedRows;
		tb->determineUnloads(comm.cardI, [&](RefRowWrapper<TB_MEN, STORE_WIN>& row) {
			if (row.isChanged || !row.isCompressed)
				row.initiateCompress(*tb);
			comm.sync.masterNotify(numThreads);
			comm.sync.masterWait(numThreads);
			row.finishCompress(*tb);
			compressedRows.push_back(&row);
		});

		U8 invCardI = CARDS_INVERT[comm.cardI];
		for (U8 decompressCardI : std::array<U8, 5>{
			comm.cardI,
			CARDS_SWAP[invCardI][1][0],
			CARDS_SWAP[invCardI][1][1],
			CARDS_SWAP[invCardI][0][0],
			CARDS_SWAP[invCardI][0][1],
		}) {
			auto& row = tb->refTable[decompressCardI];
			totalLoads++;
			if (!row.isDecompressed) {
				totalDecompressions++;
				row.initiateDecompress(*tb);
				comm.sync.masterNotify(numThreads);
				comm.sync.masterWait(numThreads);
				row.finishDecompress(*tb, keepCompressed);
			}
		}

		for (U8 decompressCardI : std::array<U8, 5>{
			comm.cardI,
			CARDS_SWAP[invCardI][1][0],
			CARDS_SWAP[invCardI][1][1],
			CARDS_SWAP[invCardI][0][0],
			CARDS_SWAP[invCardI][0][1],
		})
			assert(tb->refTable[decompressCardI].isDecompressed);

		comm.sync.masterNotify(numThreads);
		comm.sync.masterWait(numThreads);

		const float time = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count()) / 1000000;
		totalTime += time;

		U64 cnt = tb->cnt_0;
#ifdef COUNT_BOARDS
		for (auto& entry : tb->mem)
			cnt += count_resolved(entry);
		cnt -= totalBoards;
		totalBoards += cnt;
#endif


		for (U8 decompressCardI : std::array<U8, 3>{
			comm.cardI,
			CARDS_SWAP[invCardI][1][0],
			CARDS_SWAP[invCardI][1][1],
		})
			tb->refTable[decompressCardI].usesSinceModified++;

		if (modified) {
			for (U8 decompressCardI : std::array<U8, 3>{
				comm.cardI,
				CARDS_SWAP[invCardI][0][0],
				CARDS_SWAP[invCardI][0][1],
			}) {
				auto& row = tb->refTable[decompressCardI];
				row.usesSinceModified = 0;
				row.isChanged = true;
			}
			lastModified = 0;
		} else {
			keepCompressed = true;
		}
		#ifndef NO_PRINTS
			#ifdef COUNT_BOARDS
						printf("iter %3llu-%2u: %11llu boards in %.3fs\n", comm.depth, comm.cardI, cnt, time);
			#else
						printf("iter %3llu-%2u in %.3fs\n", comm.depth, comm.cardI, time);
			#endif
		#endif

		while(lastModified++ < CARDSMULT) {

			if (++comm.cardI == CARDSMULT) {
				comm.cardI = 0;
				comm.depth++;
			}

			for (U8 decompressCardI : std::array<U8, 3>{
				comm.cardI,
				CARDS_SWAP[invCardI][1][0],
				CARDS_SWAP[invCardI][1][1],
			})
				if (tb->refTable[decompressCardI].usesSinceModified < 3)
					goto foundCardCombination;
			#ifndef NO_PRINTS
				printf("skip %3llu-%2u\n", comm.depth, comm.cardI);
			#endif
		}
		// no card combinations have been modified, quit
		comm.depth = 0;
		comm.sync.masterNotify(numThreads);
		break;
	foundCardCombination:

		modified = false;

		beginTime = std::chrono::steady_clock::now();


		// std::cout << (U64)invCardI << ' ' << (U64)CARDS_SWAP[cardI][1][0] << ' ' << (U64)CARDS_SWAP[cardI][1][1] << ' ' << (U64)CARDS_SWAP[cardI][0][0] << ' ' << (U64)CARDS_SWAP[cardI][0][1] << std::endl;

		chunkCounter = 0;
	}

	for (auto& thread : threads)
		thread.join();

	const float totalInclusiveTime = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginLoopTime).count()) / 1000000;
	tb->cnt = tb->cnt_0;
	for (U64 i = CARDSMULT; i--> 0; ) {
		U8 cardI = UNLOAD_ORDER[0][i];
		auto& row = tb->refTable[cardI];
		if (!row.isDecompressed) {
			row.initiateDecompress(*tb);
			for (int j = 0; j < numThreads; j++) // TODO: thread
				row.partialDecompress(j);
			row.finishDecompress(*tb, false);
		}
		for (auto& entry : row.mem)
			tb->cnt += countResolved<STORE_WIN>(entry);

		// row.mem.~vector<std::atomic<U64>>(); // TODO: not this :P
	}
	printf("found %llu boards in %.3fs/%.3fs\n", tb->cnt, totalTime, totalInclusiveTime);


	std::cout << "decompressed " << totalDecompressions << " rows out of " << totalLoads << " loads" << std::endl;

	if ((TB_MEN == 6 && tb->cnt != 537649967ULL) || (TB_MEN == 8 && tb->cnt != 19974501547ULL)) {
		std::cerr << "ERROR: WRONG NUMBER OF BOARDS!" << std::endl;
		throw std::runtime_error("wrong number of boards");
	}

	return tb;
}



// iterate over unresolved states to find p0 wins with p1 to move. Check if all possible p1 moves result in wins for p0.
template<U16 TB_MEN, bool STORE_WIN, int depth>
__attribute__((noinline))
void singleDepthPass(const CardsInfo& cards, U8 invCardI, TableBase<TB_MEN, STORE_WIN>& tb, std::atomic<U64>& chunkCounter, bool& modified) {
	bool mod = false;
	BoardIndex bi;
	// U64 i = 0;


	U8 cardI = CARDS_INVERT[invCardI];

	auto& cardTb = tb[invCardI];
	auto& targetRow0 = tb[CARDS_SWAP[cardI][1][0]];
	auto& targetRow1 = tb[CARDS_SWAP[cardI][1][1]];
	auto& p0ReverseTargetRow0 = tb[CARDS_SWAP[cardI][0][0]];
	auto& p0ReverseTargetRow1 = tb[CARDS_SWAP[cardI][0][1]];
	auto permutation = CARDS_PERMUTATIONS[cardI];


	const MoveBoard& moveBoard_p1_card0_rev = cards.moveBoardsReverse[permutation.playerCards[1][0]];
	const MoveBoard& moveBoard_p1_card1_rev = cards.moveBoardsReverse[permutation.playerCards[1][1]];

	const MoveBoard moveBoard_p0_card01_rev = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
	const MoveBoard moveBoard_p1_card01_rev = combineMoveBoards(moveBoard_p1_card0_rev, moveBoard_p1_card1_rev);
	const MoveBoard moveBoard_p1_card01 = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[1][0]], cards.moveBoardsForward[permutation.playerCards[1][1]]);
	const MoveBoard moveBoard_p0_card1side_rev = combineMoveBoards(cards.moveBoardsReverse[permutation.sideCard], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
	const MoveBoard moveBoard_p0_card0side_rev = combineMoveBoards(cards.moveBoardsReverse[permutation.sideCard], cards.moveBoardsReverse[permutation.playerCards[0][0]]);

	const MoveBoard& moveBoard_p1_card0_or_01 = depth > 2 ? moveBoard_p1_card0_rev : moveBoard_p1_card01_rev;

	const MoveBoard& moveboard_p0_cardside_rev = cards.moveBoardsReverse[permutation.sideCard];

	while (true) {
		bi.pieceCnt_kingsIndex = CHUNK_SIZE<TB_MEN> * chunkCounter++;
		if (bi.pieceCnt_kingsIndex >= PIECECOUNTMULT<TB_MEN> * KINGSMULT) {
			[[unlikely]]
			// std::cout << i << std::endl;
			break;
		}

		bi.pieceCnt_kingsIndex--;
		for (U64 i = 0; i < CHUNK_SIZE<TB_MEN>; i++) {
			bi.pieceCnt_kingsIndex++;

			std::atomic<TB_ENTRY>* currentEntry = cardTb[bi.pieceCnt_kingsIndex];
			std::atomic<TB_ENTRY>* lastEntry = cardTb[bi.pieceCnt_kingsIndex + 1];
			if (currentEntry == lastEntry)
				continue;

			for (U64 pieceIndex = 0; currentEntry < lastEntry; pieceIndex += NUM_BOARDS_PER_ENTRY<STORE_WIN>) {
				auto& entry = *currentEntry++;
				TB_ENTRY newP1Wins = 0;
				for (auto bits = getResolvedBits<STORE_WIN>(~entry); bits; bits &= bits - 1) {
					U64 bitIndex = sizeof(bits) == 4 ? _tzcnt_u32(bits) : _tzcnt_u64(bits);
					bi.pieceIndex = pieceIndex + bitIndex;

					Board board = indexToBoard<TB_MEN, 1>(bi, moveBoard_p1_card01); // inverted because index assumes p0 to move and we are looking for the board with p1 to move

					bool isTempleThreatened = board.isTempleKingInRange<0>(moveBoard_p0_card01_rev);
					if (!(isTempleThreatened && board.isTempleFree<0>())) { // if p0 can just walk to temple
						U64 kingThreatenPawns = board.isTakeWinInOne<0>(moveBoard_p0_card01_rev);
						U64 landMaskStore = kingThreatenPawns ? kingThreatenPawns : ~board.bbp[1];
						if (isTempleThreatened)
							landMaskStore &= ~(1ULL << PTEMPLE[0]);
						U64 scan = board.bbp[1] & ~board.bbk[1];
						if (kingThreatenPawns)
							scan &= _popcnt64(kingThreatenPawns) > 1 ? 0ULL : moveBoard_p1_card01[_tzcnt_u64(kingThreatenPawns)];

						#pragma unroll
						for (int isKingMove = 0; isKingMove < 2; isKingMove++) {
							while (scan || isKingMove) {
								U64 sourcePiece = isKingMove ? board.bbk[1] : scan & -scan;
								U64 landMask = isKingMove ? ~board.bbp[1] : landMaskStore;
								U64 bbp = board.bbp[1] - sourcePiece;
								U64 pp = _tzcnt_u64(sourcePiece);
								U64 land = moveBoard_p1_card01_rev[pp] & landMask;
								if (!isKingMove && depth == 2) { // tb is empty, so if a move exists it will always be 0 in the tb
									if (land)
										goto notWin;
								} else {
									while (land) {
										U64 landPiece = land & -land;
										land &= land - 1;

										if (isKingMove) {
											if (board.isKingAttacked<0>(landPiece, moveBoard_p0_card01_rev))
												continue;
											if (depth == 2)
												goto notWin;
										}

										Board targetBoard{
											.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
											.bbk = { board.bbk[0], isKingMove ? landPiece : board.bbk[1] },
										};

										auto ti = boardToIndex<TB_MEN, 0>(targetBoard, moveBoard_p0_card01_rev); // the resulting board has p0 to move and needs to be a win

										bool oneTrue = false;
										if (landPiece & moveBoard_p1_card0_or_01[pp]) {
											oneTrue = true;
											if ((static_cast<const TB_ENTRY&>(targetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_ENTRY<STORE_WIN>]) & (1ULL << SHIFT_TO_RESOLVED_BITS<STORE_WIN> << (ti.pieceIndex % NUM_BOARDS_PER_ENTRY<STORE_WIN>))) == 0)
												goto notWin;
										}
										if (depth > 2 && (!oneTrue || landPiece & moveBoard_p1_card1_rev[pp]))
											if ((static_cast<const TB_ENTRY&>(targetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_ENTRY<STORE_WIN>]) & (1ULL << SHIFT_TO_RESOLVED_BITS<STORE_WIN> << (ti.pieceIndex % NUM_BOARDS_PER_ENTRY<STORE_WIN>))) == 0)
												goto notWin;
									}
								}
								if (isKingMove)
									break;
								scan &= scan - 1;
							}
						}
					}


					// all p1 moves result in win for p0. mark state as won for p0
					newP1Wins |= 1ULL << bitIndex;


					{ // also mark all states with p0 to move that have the option of moving to this board
						U64 pk1 = _tzcnt_u64(board.bbk[1]);
						U64 pk1Unmove0 = moveBoard_p0_card1side_rev[pk1];
						U64 pk1Unmove1 = moveBoard_p0_card0side_rev[pk1];
						U64 bbk0WinPosUnmove0 = moveBoard_p0_card1side_rev[PTEMPLE[0]];
						U64 bbk0WinPosUnmove1 = moveBoard_p0_card0side_rev[PTEMPLE[0]];

						bool kingInTempleRange0 = bbk0WinPosUnmove0 & board.bbk[0];
						bool kingInTempleRange1 = bbk0WinPosUnmove1 & board.bbk[0];
						bool templeBlockedByPawn = board.bbp[0] & (1 << PTEMPLE[0]);

						#pragma unroll
						for (int isUntakeMove = 0; isUntakeMove < 2; isUntakeMove++) {
							if (isUntakeMove && _popcnt64(board.bbp[1]) == TB_MEN / 2) // no more extra pieces allowed, don't do reverse take moves
								break;

							U64 scan = board.bbp[0] & ~board.bbk[0]; // no reverse take moves
							#pragma unroll
							for (int isKingMove = 0; isKingMove < 2; isKingMove++) {
								while (scan || isKingMove) {
									U64 sourcePiece = isKingMove ? board.bbk[0] : scan & -scan;
									U64 bbp = board.bbp[0] & ~sourcePiece;
									U64 bbp1Untake = board.bbp[1] | sourcePiece;
									U64 pp = _tzcnt_u64(sourcePiece);
									U64 land = moveboard_p0_cardside_rev[pp] & ~board.bbp[1] & ~board.bbp[0] & ~(isKingMove ? 1 << PTEMPLE[0] : 0);
									while (land) {
										U64 landPiece = land & -land;
										land &= land - 1;

										Board targetBoard{
											.bbp = { bbp | landPiece, isUntakeMove ? bbp1Untake : board.bbp[1] },
											.bbk = { isKingMove ? landPiece : board.bbk[0], board.bbk[1] },
										};

										bool isWinInOne0;
										bool isWinInOne1;
										if (!isUntakeMove) {
											if (!isKingMove) {
												isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || (kingInTempleRange0 && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
												isWinInOne1 = (pk1Unmove1 & targetBoard.bbp[0]) || (kingInTempleRange1 && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
											} else {
												isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove0 & targetBoard.bbk[0]) && !templeBlockedByPawn);
												isWinInOne1 = (pk1Unmove1 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove1 & targetBoard.bbk[0]) && !templeBlockedByPawn);
											}
										} else {
											isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove0 & targetBoard.bbk[0]) && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
											isWinInOne1 = (pk1Unmove1 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove1 & targetBoard.bbk[0]) && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
										}

										if (!isWinInOne0) {
											BoardToIndexIntermediate<TB_MEN> im;
											auto ti = boardToIndex<TB_MEN, 0>(targetBoard, moveBoard_p0_card1side_rev, im);
											p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_ENTRY<STORE_WIN>].fetch_or(getWinBits<STORE_WIN>(ti.pieceIndex % NUM_BOARDS_PER_ENTRY<STORE_WIN>), std::memory_order_relaxed);
											if (!isWinInOne1) {
												auto ti1 = boardToIndexFromIntermediate<TB_MEN, 0>(targetBoard, moveBoard_p0_card0side_rev, ti, im);
												p0ReverseTargetRow1[ti1.pieceCnt_kingsIndex][ti1.pieceIndex / NUM_BOARDS_PER_ENTRY<STORE_WIN>].fetch_or(getWinBits<STORE_WIN>(ti1.pieceIndex % NUM_BOARDS_PER_ENTRY<STORE_WIN>), std::memory_order_relaxed);
											}
										} else if (!isWinInOne1) {
											auto ti = boardToIndex<TB_MEN, 0>(targetBoard, moveBoard_p0_card0side_rev);
											p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_ENTRY<STORE_WIN>].fetch_or(getWinBits<STORE_WIN>(ti.pieceIndex % NUM_BOARDS_PER_ENTRY<STORE_WIN>), std::memory_order_relaxed);
										}
									}
									if (isKingMove)
										break;
									scan &= scan - 1;
								}
							}
						}
					}

				notWin:; // jump out of all the forward move loops, and jump over all the mark tb entry as win code
				}

				if (newP1Wins) {
					entry.fetch_or(newP1Wins, std::memory_order_relaxed);
					mod = true;
				}
			}
		}
	}
	if (mod)
		modified = true;
}