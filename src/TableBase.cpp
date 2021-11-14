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

TableBase* generateTB(const CardsInfo& cards) {


	TableBase* tb = new TableBase;
	U64 cnt_0 = 0;
	U64 totalSize = 0;
	BoardIndex bi;
	for (U64 cardI = 0; cardI < CARDSMULT; cardI++) {
		auto& cardTb = (*tb)[cardI];
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		for (U64 pieceCountI = 0; pieceCountI < PIECECOUNTMULT; pieceCountI++) {
			auto& pc = OFFSET_ORDER[pieceCountI];
			for (U64 kingI = 0; kingI < KINGSMULT; kingI++) {
				bi.cardsPieceCntKingsIndex = pieceCountI * KINGSMULT + kingI;
				U64 rowSize;

				U64 bbk0, bbk1;
				std::tie(bbk0, bbk1) = TABLES_BBKINGS[0][kingI];
				U64 ik1 = _tzcnt_u64(bbk1);

				U64 p0mask = bbk0 | bbk1 | reverseMoveBoard[ik1];

				if (reverseMoveBoard[ik1] & bbk0)
					rowSize = 0;
				else {
					bool templeWinThreatened = reverseMoveBoard[PTEMPLE[0]] & bbk0;
					if (templeWinThreatened && ((pc.first == 0) || (reverseMoveBoard[ik1] & (1 << PTEMPLE[0]))))
						rowSize = 0;
					else {
						if (templeWinThreatened)
							p0mask |= 1 << PTEMPLE[0];

						U64 p0Options = 25 - _popcnt64(p0mask);
						U64 p0Combinations = fact(p0Options, p0Options-(pc.first-templeWinThreatened)) / fact(pc.first-templeWinThreatened);
						U64 p1Combinations = fact(23-pc.first, 23-pc.first-pc.second) / fact(pc.second);
						rowSize = p0Combinations * p1Combinations;
					}
				}

				// std::cout << cardI << '\t' << pc.first << ' ' << pc.second << '\t' << kingI << '\t' << rowSize << std::endl;
				totalSize += rowSize;
				
				U64 rowEntries = (rowSize + 31) / 32;
				auto& row = cardTb[bi.cardsPieceCntKingsIndex];
				row.size = rowSize;
				if (!rowEntries)
					row.begin = nullptr;
				else {
					row.begin = new std::atomic<U64>[rowEntries]{0};
					row.begin[rowEntries - 1] = (1ULL << 32) - (1ULL << (((rowSize + 31) % 32) + 1)); // mark final rows as resolved so we dont have to worry about it
					cnt_0 -= _popcnt64(row.begin[rowEntries - 1]);
				}
			}
		}
	}
	std::cout << "Total size: " << totalSize << std::endl;

	const U64 numThreads = std::clamp<U64>(std::thread::hardware_concurrency(), 1, 1);
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

		const float time = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count()) / 1000000;
		totalTime += time;
		U64 cnt = cnt_0;
#ifdef COUNT_BOARDS
		for (auto& cardTb : *tb)
			for (auto& row : cardTb)
				for (auto val = row.begin; val < row.begin + (row.size + 31) / 32; val++)
					cnt += _popcnt32(*val);
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
	for (auto& cardTb : *tb)
		for (auto& row : cardTb)
			for (auto val = row.begin; val < row.begin + (row.size + 31) / 32; val++)
				cnt += _popcnt32(*val);
	printf("found %llu boards in %.3fs/%.3fs\n", cnt, totalTime, totalInclusiveTime);

	return tb;
}





// iterate over unresolved states to find p0 wins with p1 to move. Check if all possible p1 moves result in wins for p0.
template<int depth>
void singleDepthPass(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified) {
	bool mod = false;
	BoardIndex bi;
	while (true) {
		bi.cardsPieceCntKingsIndex = chunkCounter++;
		if (bi.cardsPieceCntKingsIndex >= tb.size())
			break;

		// check if all P1 moves lead to a victory
		U64 cardI = bi.cardsPieceCntKingsIndex % (PIECECOUNTMULT * KINGSMULT);
		U64 invCardI = CARDS_INVERT[cardI];
		auto& cardTb = tb[invCardI];
		auto& row = cardTb[bi.cardsPieceCntKingsIndex];
		if (!row.size)
			continue;

		auto permutation = CARDS_PERMUTATIONS[cardI];
		// forward moves for p1 so reverse moveboards
		const MoveBoard& moveBoard1 = cards.moveBoardsReverse[permutation.playerCards[1][1]];
		const MoveBoard& moveBoard0 = depth > 2 ? cards.moveBoardsReverse[permutation.playerCards[1][0]] : combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[1][0]], moveBoard1);
		auto& targetRow0 = tb[CARDS_SWAP[cardI][1][0]];
		auto& targetRow1 = tb[CARDS_SWAP[cardI][1][1]];
		const MoveBoard combinedMoveBoard = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[0][0]], cards.moveBoardsForward[permutation.playerCards[0][1]]);
		const MoveBoard combinedMoveBoardFlip = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard combinedOtherMoveBoard = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[1][0]], cards.moveBoardsForward[permutation.playerCards[1][1]]);
		const MoveBoard combinedOtherMoveBoardFlip = combineMoveBoards(moveBoard0, moveBoard1);

		const MoveBoard combinedMoveBoardsFlipUnmove0 = combineMoveBoards(cards.moveBoardsReverse[permutation.sideCard], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard combinedMoveBoardsFlipUnmove1 = combineMoveBoards(cards.moveBoardsReverse[permutation.sideCard], cards.moveBoardsReverse[permutation.playerCards[0][0]]);
		const MoveBoard combinedMoveBoardsUnmove0 = combineMoveBoards(cards.moveBoardsForward[permutation.sideCard], cards.moveBoardsForward[permutation.playerCards[0][1]]);
		const MoveBoard combinedMoveBoardsUnmove1 = combineMoveBoards(cards.moveBoardsForward[permutation.sideCard], cards.moveBoardsForward[permutation.playerCards[0][0]]);

		// moveboard for reversing p0
		const MoveBoard& p0ReverseMoveBoard = cards.moveBoardsReverse[permutation.sideCard];
		auto& p0ReverseTargetRow0 = tb[CARDS_SWAP[cardI][0][0]];
		auto& p0ReverseTargetRow1 = tb[CARDS_SWAP[cardI][0][1]];

		for (bi.pieceIndex = 0; bi.pieceIndex < row.size; bi.pieceIndex++) {
			auto& entry = row.begin[bi.pieceIndex / 32];
			U64 newP1Wins = 0;
			for (U32 bits = ~entry; bits; bits &= bits - 1) {
				U64 bitIndex = _tzcnt_u64(bits);
				Board board = indexToBoard<true>(bi, combinedOtherMoveBoardFlip); // inverted because index assumes p0 to move and we are looking for the board with p1 to move

				if (board.isWinInOne<true>(combinedOtherMoveBoardFlip)) {
					std::cout << "oops.." << std::endl;
				}

				// move p1 board, test if all moves result in won boards for p0.

				bool isTempleThreatened = board.isTempleKingInRange<0>(combinedMoveBoardFlip);
				if (!(isTempleThreatened && board.isTempleFree<0>())) { // if p0 can just walk to temple
					U64 kingThreatenPawns = board.isTakeWinInOne<0>(combinedMoveBoardFlip);
					U64 scan = board.bbp[1] & ~board.bbk[1];
					while (scan) {
						U64 sourcePiece = scan & -scan;
						U64 landMask = kingThreatenPawns ? kingThreatenPawns : ~board.bbp[1];
						U64 bbp = board.bbp[1] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						U64 land = combinedOtherMoveBoardFlip[pp] & landMask;
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

								auto ti = boardToIndex<false>(targetBoard, combinedMoveBoard);
								// the resulting board has p0 to move and needs to be a win
								bool oneTrue = false;
								if (landPiece & moveBoard0[pp]) {
									oneTrue = true;
									if ((targetRow0[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].load(std::memory_order_relaxed) & (1ULL << (ti.pieceIndex % 32))) == 0)
										goto notWin;
								}
								if (!oneTrue || landPiece & moveBoard1[pp]) {
									if ((targetRow1[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].load(std::memory_order_relaxed) & (1ULL << (ti.pieceIndex % 32))) == 0)
										goto notWin;
								}
							}
						}
						scan &= scan - 1;
					}
					{ // king move
						U64 sourcePiece = board.bbk[1];
						U64 landMask = ~board.bbp[1];
						U64 bbp = board.bbp[1] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						U64 land = combinedOtherMoveBoardFlip[pp] & landMask;
						while (land) {
							U64 landPiece = land & -land;
							land &= land - 1;
							Board targetBoard{
								.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
								.bbk = { board.bbk[0], landPiece },
							};

							if (targetBoard.isTakeWinInOne<false>(combinedMoveBoardFlip))
								continue;

							if (depth == 2)
								goto notWin;

							auto ti = boardToIndex<false>(targetBoard, combinedMoveBoard);
							bool oneTrue = false;
							if (landPiece & moveBoard0[pp]) {
								oneTrue = true;
								if ((targetRow0[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].load(std::memory_order_relaxed) & (1ULL << (ti.pieceIndex % 32))) == 0)
									goto notWin;
							}
								
							if (!oneTrue || (landPiece & moveBoard1[pp])) {
								if ((targetRow1[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].load(std::memory_order_relaxed) & (1ULL << (ti.pieceIndex % 32))) == 0)
									goto notWin;
							}
						}
					}
				}

				// at this point all the p1 unmoves are verified to be won for p0.
				// mark this board as a loss for p1.
				newP1Wins |= 0x000000001ULL << bitIndex;

				// also mark all boards resulting from unmoves as a win for p0.
				{ 
					U64 pk1 = _tzcnt_u64(board.bbk[1]);
					U64 pk1Unmove0 = combinedMoveBoardsFlipUnmove0[pk1];
					U64 pk1Unmove1 = combinedMoveBoardsFlipUnmove1[pk1];
					U64 bbk0WinPosUnmove0 = combinedMoveBoardsFlipUnmove0[PTEMPLE[0]];
					U64 bbk0WinPosUnmove1 = combinedMoveBoardsFlipUnmove1[PTEMPLE[0]];

					bool kingInTempleRange0 = bbk0WinPosUnmove0 & board.bbk[0];
					bool kingInTempleRange1 = bbk0WinPosUnmove1 & board.bbk[0];
					bool pawnTempleBlock = board.bbp[0] & (1 << PTEMPLE[0]);
							
					U64 scan = board.bbp[0] & ~board.bbk[0]; // no reverse take moves
					while (scan) { // pawn unmoves
						U64 sourcePiece = scan & -scan;
						U64 bbp = board.bbp[0] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						U64 land = p0ReverseMoveBoard[pp] & ~board.bbp[1] & ~board.bbp[0];
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
								auto ti = boardToIndex<false>(targetBoard, combinedMoveBoardsUnmove0);
								p0ReverseTargetRow0[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
							}
							if (!isWinInOne1) {
								auto ti = boardToIndex<false>(targetBoard, combinedMoveBoardsUnmove1);
								p0ReverseTargetRow1[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
							}
						}
						scan &= scan - 1;
					}
					{ // king unmove
						U64 sourcePiece = board.bbk[0];
						U64 bbp = board.bbp[0] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						U64 land = p0ReverseMoveBoard[pp] & ~board.bbp[1] & ~board.bbp[0] & ~(1 << PTEMPLE[0]);
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
								auto ti = boardToIndex<false>(targetBoard, combinedMoveBoardsUnmove0);
								p0ReverseTargetRow0[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
							}
							if (!isWinInOne1) {
								auto ti = boardToIndex<false>(targetBoard, combinedMoveBoardsUnmove1);
								p0ReverseTargetRow1[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
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
							U64 land = p0ReverseMoveBoard[pp] & ~board.bbp[1] & ~board.bbp[0];
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
									auto ti = boardToIndex<false>(targetBoard, combinedMoveBoardsUnmove0);
									p0ReverseTargetRow0[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								}
								if (!isWinInOne1) {
									auto ti = boardToIndex<false>(targetBoard, combinedMoveBoardsUnmove1);
									p0ReverseTargetRow1[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								}
							}
							scan &= scan - 1;
						}
						{ // king unmove
							U64 sourcePiece = board.bbk[0];
							U64 bbp = board.bbp[0] - sourcePiece;
							U64 bbp1 = board.bbp[1] | sourcePiece;
							U64 pp = _tzcnt_u64(sourcePiece);
							U64 land = p0ReverseMoveBoard[pp] & ~board.bbp[1] & ~board.bbp[0] & ~(1 << PTEMPLE[0]);
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
									auto ti = boardToIndex<false>(targetBoard, combinedMoveBoardsUnmove0);
									p0ReverseTargetRow0[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								}
								if (!isWinInOne1) {
									auto ti = boardToIndex<false>(targetBoard, combinedMoveBoardsUnmove1);
									p0ReverseTargetRow1[ti.cardsPieceCntKingsIndex].begin[ti.pieceIndex / 32].fetch_or(0x100000001ULL << (ti.pieceIndex % 32), std::memory_order_relaxed);
								}
							}
						}
					}
				}

			notWin:;
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