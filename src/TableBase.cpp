#include "TableBase.h"

#include "Board.h"
#include "Index.h"
#include "Card.hpp"
#include "Config.h"

#include <vector>
#include <atomic>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iostream>
#include <cassert>
#include <immintrin.h>
#include <x86intrin.h>






template<int depth>
void singleDepthPass(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified);

std::unique_ptr<TableBase> generateTB(const CardsInfo& cards) {

	auto tb = std::make_unique<TableBase>();
	
	std::cout << "jump table size: " << sizeof(tb->refTable) / sizeof(void*) << " entries (" << sizeof(tb->refTable) / 1024 << "KB)" << std::endl;

	U64 cnt_0 = 0, totalRows = 0, totalSize = 0;
	for (U64 cardI = 0; cardI < CARDSMULT; cardI++) {
		auto& cardTb = (*tb)[cardI];
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);

		iterateTBCounts(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
			totalRows += (rowSize + 31) / 32;
			totalSize += rowSize;
		});
	}
	
	std::cout << "main table size: " << totalSize << " entries (" << totalRows * sizeof(U64) / 1024 / 1024 << "MB)" << std::endl;
	
	try {
		tb->mem = std::vector<std::atomic<U64>>(totalRows);
	} catch (const std::bad_alloc& e) {
		std::cout << e.what() << " (not enough memory?)" << std::endl;
		throw e;
	}
	std::atomic<U64>* tbMemPtrIncr = tb->mem.data();

	for (U64 cardI = 0; cardI < CARDSMULT; cardI++) {
		auto& cardTb = (*tb)[cardI];
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard combinedOtherMoveBoard = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[0][0]], cards.moveBoardsForward[permutation.playerCards[0][1]]);

		iterateTBCounts(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
			auto& row = cardTb[pieceCnt_kingsIndex];
			row = tbMemPtrIncr;
			if (rowSize == 0)
				return;
			U64 rowEntries = (rowSize + 31) / 32;
			row[rowEntries - 1] = (1ULL << 32) - (1ULL << (((rowSize + 31) % 32) + 1)); // mark final rows as resolved so we dont have to worry about it
			// if (cardI == 0 && pieceCnt_kingsIndex < 5)
			// 	std::cout << cardI << ' ' << pieceCnt_kingsIndex << ' ' << std::hex << tbMemPtrIncr << ' ' << row[rowEntries - 1] << std::endl;
			cnt_0 -= _popcnt32(row[rowEntries - 1]);
			tbMemPtrIncr += rowEntries;
		});

		cardTb.back() = tbMemPtrIncr;
	}

	U64 numThreads = std::clamp<U64>(std::thread::hardware_concurrency(), 1, 1024);
	std::vector<std::thread> threads(numThreads);

	U64 depth = 2;
	U64 totalBoards = 0;
	float totalTime = 0;
	auto beginLoopTime = std::chrono::steady_clock::now();
	while (true) {
		auto beginTime = std::chrono::steady_clock::now();

		bool modified = false;
		std::atomic<U64> chunkCounter = 0;

		void (*targetFn)(const CardsInfo&, TableBase&, std::atomic<U64>&, bool&);
		if (depth == 2) targetFn = &singleDepthPass<2>;
		// else if (depth == 3) targetFn = &singleDepthPass<3>;
		else            targetFn = &singleDepthPass<3>;

		for (U64 i = 0; i < numThreads; i++)
			threads[i] = std::thread(targetFn, std::cref(cards), std::ref(*tb), std::ref(chunkCounter), std::ref(modified));
		for (auto& thread : threads)
			thread.join();
		//numThreads = 1;

		const float time = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count()) / 1000000;
		totalTime += time;
		U64 cnt = cnt_0;
#ifdef COUNT_BOARDS
		for (auto& entry : tb->mem)
			cnt += _popcnt32(entry);
		cnt -= totalBoards;
		totalBoards += cnt;
#endif
		if (!modified)
			break;
#ifdef COUNT_BOARDS
			printf("iter %3llu: %11llu boards in %.3fs\n", depth, cnt, time);
#else
			printf("iter %3llu in %.3fs\n", depth, time);
#endif
		depth++;
	}
	const float totalInclusiveTime = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginLoopTime).count()) / 1000000;
	U64 cnt = cnt_0;
	for (auto& entry : tb->mem)
		cnt += _popcnt32(entry);
	printf("found %llu boards in %.3fs/%.3fs\n", cnt, totalTime, totalInclusiveTime);

	return tb;
}




// iterate over unresolved states to find p0 wins with p1 to move. Check if all possible p1 moves result in wins for p0.
template<int depth>
void singleDepthPass(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified) {
	bool mod = false;
	BoardIndex bi;
	U64 i = 0;
	while (true) {
		U64 work = chunkCounter++;
		if (work >= PIECECOUNTMULT * KINGSMULT * CARDSMULT)
			break;
		bi.pieceCnt_kingsIndex = work % (PIECECOUNTMULT * KINGSMULT);

		U64 invCardI = work / (PIECECOUNTMULT * KINGSMULT);
		auto& cardTb = tb[invCardI];
		std::atomic<U64>* currentEntry = cardTb[bi.pieceCnt_kingsIndex];
		std::atomic<U64>* lastEntry = cardTb[bi.pieceCnt_kingsIndex + 1];
		if (currentEntry == lastEntry)
			continue;

		// { 0, 4, 1, 2, 3 }, //BOAR, OX, ELEPHANT, HORSE, CRAB
		U64 cardI = CARDS_INVERT[invCardI];
		auto permutation = CARDS_PERMUTATIONS[cardI]; // cardI = 5: p0: BOAR CRAB, p1: OX ELEPHANT, swap: HORSE
		// forward moves for p1 so reverse moveboards
		const MoveBoard& moveBoard_p1_card1 = cards.moveBoardsReverse[permutation.playerCards[1][1]];
		const MoveBoard& moveBoard_p1_card0 = depth > 2 ? cards.moveBoardsReverse[permutation.playerCards[1][0]] : combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[1][0]], moveBoard_p1_card1);
		auto& targetRow0 = tb[CARDS_SWAP[cardI][1][0]];
		auto& targetRow1 = tb[CARDS_SWAP[cardI][1][1]];
		const MoveBoard moveBoard_p0_card01_flip = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard moveBoard_p1_card01 = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[1][0]], moveBoard_p1_card1);
		const MoveBoard moveBoard_p1_card01_flip = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[1][0]], cards.moveBoardsForward[permutation.playerCards[1][1]]);
		
		const MoveBoard moveBoard_p0_card1side_flip = combineMoveBoards(cards.moveBoardsReverse[permutation.sideCard], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard moveBoard_p0_card0side_flip = combineMoveBoards(cards.moveBoardsReverse[permutation.sideCard], cards.moveBoardsReverse[permutation.playerCards[0][0]]);

		// moveboard for reversing p0
		const MoveBoard& moveboard_p0_cardside_flip = cards.moveBoardsReverse[permutation.sideCard];
		auto& p0ReverseTargetRow0 = tb[CARDS_SWAP[cardI][0][0]];
		auto& p0ReverseTargetRow1 = tb[CARDS_SWAP[cardI][0][1]];

		for (U64 pieceIndex = 0; currentEntry < lastEntry; pieceIndex += 32) {
			auto& entry = *currentEntry++;
			U64 newP1Wins = 0;
			for (U32 bits = ~entry; bits; bits &= bits - 1) {
				// if (++i >= 149035927)
				// 	std::cout << std::endl;
				U64 win = 0;
				U64 bitIndex = _tzcnt_u64(bits);
				bi.pieceIndex = pieceIndex + bitIndex;
				Board board = indexToBoard<1>(bi, moveBoard_p1_card01_flip); // inverted because index assumes p0 to move and we are looking for the board with p1 to move
				// if (++i >= 149035927) {
				//  	board.print();
				// 	std::cout << board.isWinInOne<1>(combinedOtherMoveBoard) << ' ' << cardI << ' ' << (U64)permutation.playerCards[0][0] << ' ' << (U64)permutation.playerCards[0][1] << std::endl;
				// 	// print(combinedOtherMoveBoard);
				// 	// print(cards.moveBoardsForward[permutation.playerCards[0][0]]);
				// 	// print(cards.moveBoardsForward[permutation.playerCards[0][1]]);
				// }
				bool isTempleThreatened = board.isTempleKingInRange<0>(moveBoard_p0_card01_flip);
				if (!(isTempleThreatened && board.isTempleFree<0>())) { // if p0 can just walk to temple
					U64 kingThreatenPawns = board.isTakeWinInOne<0>(moveBoard_p0_card01_flip);
					U64 scan = board.bbp[1] & ~board.bbk[1];
					while (scan) {
						U64 sourcePiece = scan & -scan;
						U64 landMask = kingThreatenPawns ? kingThreatenPawns : ~board.bbp[1];
						U64 bbp = board.bbp[1] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						U64 land = moveBoard_p1_card01[pp] & landMask;
						if (depth == 2) {
							if (land)
								goto notWin;
						} else {
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;
								Board targetBoard{
									.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
									.bbk = { board.bbk[0], board.bbk[1] },
								};
								
								if (targetBoard.isWinInOne<false>(moveBoard_p0_card01_flip)) { //temporary solution, not optimised (this is because some edge cases were allowed to fall trough)
									continue;
								}

								auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card01_flip); // the resulting board has p0 to move and needs to be a win
									
								bool oneTrue = false;
								if (landPiece & moveBoard_p1_card0[pp]) {
									oneTrue = true;
									if ((targetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].load(std::memory_order_relaxed) & (1ULL << (ti.pieceIndex % 32))) == 0)
										goto notWin;
								}
								if (!oneTrue || landPiece & moveBoard_p1_card1[pp])
									if ((targetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].load(std::memory_order_relaxed) & (1ULL << (ti.pieceIndex % 32))) == 0)
										goto notWin;
							}
						}
						scan &= scan - 1;
					}
					{ // king move
						U64 sourcePiece = board.bbk[1];
						U64 landMask = ~board.bbp[1];
						U64 bbp = board.bbp[1] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						U64 land = moveBoard_p1_card01[pp] & landMask;
						while (land) {
							U64 landPiece = land & -land;
							land &= land - 1;
							Board targetBoard{
								.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
								.bbk = { board.bbk[0], landPiece },
							};

							if (targetBoard.isTakeWinInOne<false>(moveBoard_p0_card01_flip))
								continue;

							if (depth == 2)
								goto notWin;

							auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card01_flip); // the resulting board has p0 to move and needs to be a win

							bool oneTrue = false;
							if (landPiece & moveBoard_p1_card0[pp]) {
								oneTrue = true;
								if ((targetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].load(std::memory_order_relaxed) & (1ULL << (ti.pieceIndex % 32))) == 0)
									goto notWin;
							}
								
							if (!oneTrue || (landPiece & moveBoard_p1_card1[pp]))
								if ((targetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].load(std::memory_order_relaxed) & (1ULL << (ti.pieceIndex % 32))) == 0)
									goto notWin;
						}
					}
				}

				
				// if (depth == 2 && !board.isWinInTwo<false>(combinedMoveBoardFlip, combinedOtherMoveBoardFlip)) {
				// 	if (board.isKingAttacked<false>(board.bbk[1], combinedMoveBoardFlip)) {
				// 		board.print();
				// 		print(combinedMoveBoardFlip);
						
				// 		print(combinedOtherMoveBoardFlip);
				// 		board.isWinInTwo<false>(combinedMoveBoardFlip, combinedOtherMoveBoardFlip);
				// 	}
				// }

				// all p1 moves result in win for p0. mark state as won for p0
				newP1Wins |= 0x000000001ULL << bitIndex;

				
				{ // also mark all states with p0 to move that have the option of moving to this board
					U64 pk1 = _tzcnt_u64(board.bbk[1]);
					U64 pk1Unmove0 = moveBoard_p0_card1side_flip[pk1];
					U64 pk1Unmove1 = moveBoard_p0_card0side_flip[pk1];
					U64 bbk0WinPosUnmove0 = moveBoard_p0_card1side_flip[PTEMPLE[0]];
					U64 bbk0WinPosUnmove1 = moveBoard_p0_card0side_flip[PTEMPLE[0]];

					bool kingInTempleRange0 = bbk0WinPosUnmove0 & board.bbk[0];
					bool kingInTempleRange1 = bbk0WinPosUnmove1 & board.bbk[0];
					bool pawnTempleBlock = board.bbp[0] & (1 << PTEMPLE[0]);
							
					U64 scan = board.bbp[0] & ~board.bbk[0]; // no reverse take moves
					while (scan) { // pawn unmoves
						U64 sourcePiece = scan & -scan;
						U64 bbp = board.bbp[0] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						U64 land = moveboard_p0_cardside_flip[pp] & ~board.bbp[1] & ~board.bbp[0];
						while (land) {
							U64 landPiece = land & -land;
							land &= land - 1;
							Board targetBoard{
								.bbp = { bbp | landPiece, board.bbp[1] },
								.bbk = { board.bbk[0], board.bbk[1] },
							};

							// bool isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || (kingMove && (landPiece & bbk0WinPosUnmove0));
							bool isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || (kingInTempleRange0 && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
							bool isWinInOne1 = (pk1Unmove1 & targetBoard.bbp[0]) || (kingInTempleRange1 && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
							if (!isWinInOne0) {
								auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card1side_flip);
								p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								if (!isWinInOne1)
									p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
							} else {
								if (!isWinInOne1) {
									auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card0side_flip);
									p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								}

							}
						}
						scan &= scan - 1;
					}
					{ // king unmove
						U64 sourcePiece = board.bbk[0];
						U64 bbp = board.bbp[0] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						U64 land = moveboard_p0_cardside_flip[pp] & ~board.bbp[1] & ~board.bbp[0] & ~(1 << PTEMPLE[0]);
						while (land) {
							U64 landPiece = land & -land;
							land &= land - 1;
							Board targetBoard{
								.bbp = { bbp | landPiece, board.bbp[1] },
								.bbk = { landPiece, board.bbk[1] },
							};

							// bool isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || (kingMove && (landPiece & bbk0WinPosUnmove0));
							bool isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove0 & targetBoard.bbk[0]) && !pawnTempleBlock);
							bool isWinInOne1 = (pk1Unmove1 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove1 & targetBoard.bbk[0]) && !pawnTempleBlock);
							if (!isWinInOne0) {
								auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card1side_flip);
								p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								if (!isWinInOne1)
									p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
							} else {
								if (!isWinInOne1) {
									auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card0side_flip);
									p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								}

							}
						}
					}
					
					
					if (_popcnt64(board.bbp[1]) < TB_MEN / 2) { // reverse take move
						scan = board.bbp[0] & ~board.bbk[0];
						while (scan) { // pawn unmoves
							U64 sourcePiece = scan & -scan;
							U64 bbp = board.bbp[0] - sourcePiece;
							U64 bbp1 = board.bbp[1] | sourcePiece;
							U64 pp = _tzcnt_u64(sourcePiece);
							U64 land = moveboard_p0_cardside_flip[pp] & ~board.bbp[1] & ~board.bbp[0];
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;
								Board targetBoard{
									.bbp = { bbp | landPiece, bbp1 },
									.bbk = { board.bbk[0], board.bbk[1] },
								};
								
								bool isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove0 & targetBoard.bbk[0]) && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
								bool isWinInOne1 = (pk1Unmove1 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove1 & targetBoard.bbk[0]) && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
								if (!isWinInOne0) {
									auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card1side_flip);
									p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
									if (!isWinInOne1)
										p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								} else {
									if (!isWinInOne1) {
										auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card0side_flip);
										p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
									}

								}
							}
							scan &= scan - 1;
						}
						{ // king unmove
							U64 sourcePiece = board.bbk[0];
							U64 bbp = board.bbp[0] - sourcePiece;
							U64 bbp1 = board.bbp[1] | sourcePiece;
							U64 pp = _tzcnt_u64(sourcePiece);
							U64 land = moveboard_p0_cardside_flip[pp] & ~board.bbp[1] & ~board.bbp[0] & ~(1 << PTEMPLE[0]);
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;
								Board targetBoard{
									.bbp = { bbp | landPiece, bbp1 },
									.bbk = { landPiece, board.bbk[1] },
								};
								
								bool isWinInOne0 = (pk1Unmove0 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove0 & targetBoard.bbk[0]) && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
								bool isWinInOne1 = (pk1Unmove1 & targetBoard.bbp[0]) || ((bbk0WinPosUnmove1 & targetBoard.bbk[0]) && (!(targetBoard.bbp[0] & (1 << PTEMPLE[0]))));
								if (!isWinInOne0) {
									auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card1side_flip);
									p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
									if (!isWinInOne1)
										p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								} else {
									if (!isWinInOne1) {
										auto ti = boardToIndex<false>(targetBoard, moveBoard_p0_card0side_flip);
										p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
									}

								}
							}
						}
					}
				}

				 win = 1;
			 notWin:;
				//  if (i >= 149035927) {
				// 	 std::cout << i << ' ' << std::hex << cardI << ' ' << board.bbp[0] << ' ' << board.bbp[1] << ' ' << (board.bbk[0] || board.bbk[1]) << std::endl;
				// 	 std::cout << win;
				// 	 std::cout << std::endl;
				//  }
			}

			if (newP1Wins) {
				entry.fetch_or(newP1Wins, std::memory_order_relaxed);
				mod = true;
			}
		}
	}
	if (mod)
		modified = true;
}