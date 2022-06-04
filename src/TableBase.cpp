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



constexpr U64 CHUNK_SIZE = 9; // a divisor of PIECECOUNTMULT * KINGSMULT (4977) is used

template<int depth>
void singleDepthPass(const CardsInfo& cards, U16 cardI, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified);

std::unique_ptr<TableBase> generateTB(const CardsInfo& cards) {

	auto tb = std::make_unique<TableBase>();
	
	std::cout << "jump table size: " << sizeof(TableBase::RefTable) / sizeof(void*) << " entries (" << sizeof(TableBase::RefTable) / 1024 << "KB)" << std::endl;

	U64 totalRows = 0, totalSize = 0;
	tb->cnt_0 = 0;
	for (U64 cardI = CARDSMULT; cardI--> 0; ) {
		RefRowWrapper& cardTb = (*tb)[cardI];
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard combinedOtherMoveBoard = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[0][0]], cards.moveBoardsForward[permutation.playerCards[0][1]]);


		U32 rows = 0, size = 0;
		iterateTBCounts(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
			rows += (rowSize + NUM_BOARDS_PER_U64 - 1) / NUM_BOARDS_PER_U64;
			size += rowSize;
		});

		totalRows += rows;
		totalSize += size;

			
		try {
			cardTb.allocateDecompressed(rows, *tb, cardI);
			cardTb.isCompressed = false;
		} catch (const std::bad_alloc& e) {
			std::cout << e.what() << " (not enough memory?)" << std::endl;
			throw e;
		}
		U32 passedRowsCount = 0;
		// set final bits to resolved.
		iterateTBCounts(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
			cardTb.refs[pieceCnt_kingsIndex] = passedRowsCount;
			if (rowSize == 0)
				return;
			U64 rowEntries = (rowSize + NUM_BOARDS_PER_U64 - 1) / NUM_BOARDS_PER_U64;
			passedRowsCount += rowEntries;
			cardTb.mem[passedRowsCount - 1] = (NUM_BOARDS_PER_U64 < 64 ? 1ULL << NUM_BOARDS_PER_U64 : 0) - (1ULL << (((rowSize + NUM_BOARDS_PER_U64 - 1) % NUM_BOARDS_PER_U64) + 1)); // mark final rows as resolved so we dont have to worry about it
			tb->cnt_0 -= countResolved(cardTb.mem[passedRowsCount - 1]);
		});

		cardTb.refs.back() = passedRowsCount;

		// cardTb.compress();
	}
	
	std::cout << "main table size: " << totalSize << " entries (" << totalRows * sizeof(U64) / 1024 / 1024 << "MB)" << std::endl;


	U64 numThreads = std::clamp<U64>(std::thread::hardware_concurrency(), 1, 1024);
	std::vector<std::thread> threads(numThreads);

	U64 depth = 2;
	U64 totalBoards = 0;
	float totalTime = 0;
	auto beginLoopTime = std::chrono::steady_clock::now();
	while (true) {
		auto beginTime = std::chrono::steady_clock::now();

		bool modified = false;

		void (*targetFn)(const CardsInfo&, U16, TableBase&, std::atomic<U64>&, bool&);
		if (depth == 2) targetFn = &singleDepthPass<2>;
		// else if (depth == 3) targetFn = &singleDepthPass<3>;
		else            targetFn = &singleDepthPass<3>;

		for (U16 invCardI = 0; invCardI < CARDSMULT; invCardI++) {
			// std::cout << depth << ' ' << invCardI << std::endl;
			U64 cardI = CARDS_INVERT[invCardI];

			(*tb)[invCardI].decompress(*tb, invCardI);
			(*tb)[CARDS_SWAP[cardI][1][0]].decompress(*tb, invCardI);
			(*tb)[CARDS_SWAP[cardI][1][1]].decompress(*tb, invCardI);
			(*tb)[CARDS_SWAP[cardI][0][0]].decompress(*tb, invCardI);
			(*tb)[CARDS_SWAP[cardI][0][1]].decompress(*tb, invCardI);

			assert(!(*tb)[invCardI].isCompressed);
			assert(!(*tb)[CARDS_SWAP[cardI][1][0]].isCompressed);
			assert(!(*tb)[CARDS_SWAP[cardI][1][1]].isCompressed);
			assert(!(*tb)[CARDS_SWAP[cardI][0][0]].isCompressed);
			assert(!(*tb)[CARDS_SWAP[cardI][0][1]].isCompressed);


			// std::cout << (U64)invCardI << ' ' << (U64)CARDS_SWAP[cardI][1][0] << ' ' << (U64)CARDS_SWAP[cardI][1][1] << ' ' << (U64)CARDS_SWAP[cardI][0][0] << ' ' << (U64)CARDS_SWAP[cardI][0][1] << std::endl;
			
			std::atomic<U64> chunkCounter = 0;
			for (U64 i = 0; i < numThreads; i++)
				threads[i] = std::thread(targetFn, std::cref(cards), invCardI, std::ref(*tb), std::ref(chunkCounter), std::ref(modified));
			for (auto& thread : threads)
				thread.join();
				
			// (*tb)[invCardI].compress();
			// (*tb)[CARDS_SWAP[cardI][1][0]].compress();
			// (*tb)[CARDS_SWAP[cardI][1][1]].compress();
			// (*tb)[CARDS_SWAP[cardI][0][0]].compress();
			// (*tb)[CARDS_SWAP[cardI][0][1]].compress();
		}
		//numThreads = 1;

		const float time = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count()) / 1000000;
		totalTime += time;
		U64 cnt = tb->cnt_0;
#ifdef COUNT_BOARDS
		for (auto& entry : tb->mem)
			cnt += count_resolved(entry);
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
	tb->cnt = tb->cnt_0;
	for (U64 i = CARDSMULT; i--> 0; ) {
		auto& row = tb->refTable[i];
		row.decompress(*tb, i);
		for (auto& entry : row.mem)
			tb->cnt += countResolved(entry);
		// row.compress();
	}
	printf("found %llu boards in %.3fs/%.3fs\n", tb->cnt, totalTime, totalInclusiveTime);

	
	std::cout << "decompressed a row " << totalDecompressions << " times" << std::endl;

	return tb;
}



// iterate over unresolved states to find p0 wins with p1 to move. Check if all possible p1 moves result in wins for p0.
template<int depth>
void singleDepthPass(const CardsInfo& cards, U16 invCardI, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified) {
	bool mod = false;
	BoardIndex bi;
	// U64 i = 0;

	
	U64 cardI = CARDS_INVERT[invCardI];

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
		bi.pieceCnt_kingsIndex = CHUNK_SIZE * chunkCounter++;
		if (bi.pieceCnt_kingsIndex >= PIECECOUNTMULT * KINGSMULT) {
			[[unlikely]]
			// std::cout << i << std::endl;
			break;
		}
		
		bi.pieceCnt_kingsIndex--;
		for (U64 i = 0; i < CHUNK_SIZE; i++) {
			bi.pieceCnt_kingsIndex++;

			std::atomic<U64>* currentEntry = cardTb[bi.pieceCnt_kingsIndex];
			std::atomic<U64>* lastEntry = cardTb[bi.pieceCnt_kingsIndex + 1];
			if (currentEntry == lastEntry)
				continue;

			for (U64 pieceIndex = 0; currentEntry < lastEntry; pieceIndex += NUM_BOARDS_PER_U64) {
				auto& entry = *currentEntry++;
				U64 newP1Wins = 0;
				for (auto bits = getResolvedBits(~entry); bits; bits &= bits - 1) {
					U64 bitIndex = _tzcnt_u64(bits);
					bi.pieceIndex = pieceIndex + bitIndex;

					Board board = indexToBoard<1>(bi, moveBoard_p1_card01); // inverted because index assumes p0 to move and we are looking for the board with p1 to move

					bool isTempleThreatened = board.isTempleKingInRange<0>(moveBoard_p0_card01_rev);
					if (!(isTempleThreatened && board.isTempleFree<0>())) { // if p0 can just walk to temple
						U64 kingThreatenPawns = board.isTakeWinInOne<0>(moveBoard_p0_card01_rev);
						U64 landMaskStore = kingThreatenPawns ? kingThreatenPawns : ~board.bbp[1];
						if (isTempleThreatened)
							landMaskStore &= ~(1ULL << PTEMPLE[0]);
						U64 scan = board.bbp[1] & ~board.bbk[1];
						if (kingThreatenPawns)
							scan &= _popcnt64(kingThreatenPawns) > 1 ? 0ULL : moveBoard_p1_card01[_tzcnt_u64(kingThreatenPawns)];
						while (scan) {
							U64 sourcePiece = scan & -scan;
							U64 landMask = landMaskStore;
							U64 bbp = board.bbp[1] - sourcePiece;
							U64 pp = _tzcnt_u64(sourcePiece);
							U64 land = moveBoard_p1_card01_rev[pp] & landMask;
							if (depth == 2) { // tb is empty, so any land bit will be 0
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

									auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card01_rev); // the resulting board has p0 to move and needs to be a win
										
									bool oneTrue = false;
									if (landPiece & moveBoard_p1_card0_or_01[pp]) {
										oneTrue = true;
									if ((targetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].load(std::memory_order_relaxed) & (1ULL << (STORE_WIN ? 32 : 0) << (ti.pieceIndex % NUM_BOARDS_PER_U64))) == 0)
											goto notWin;
									}
									if (depth > 2 && (!oneTrue || landPiece & moveBoard_p1_card1_rev[pp]))
										if ((targetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].load(std::memory_order_relaxed) & (1ULL << (STORE_WIN ? 32 : 0) << (ti.pieceIndex % NUM_BOARDS_PER_U64))) == 0)
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
							U64 land = moveBoard_p1_card01_rev[pp] & landMask;
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;

								if (board.isKingAttacked<0>(landPiece, moveBoard_p0_card01_rev))
									continue;

								Board targetBoard{
									.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
									.bbk = { board.bbk[0], landPiece },
								};

								if (depth == 2)
									goto notWin;

								auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card01_rev); // the resulting board has p0 to move and needs to be a win

								bool oneTrue = false;
								if (landPiece & moveBoard_p1_card0_or_01[pp]) {
									oneTrue = true;
									if ((targetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].load(std::memory_order_relaxed) & (1ULL << (STORE_WIN ? 32 : 0) << (ti.pieceIndex % NUM_BOARDS_PER_U64))) == 0)
										goto notWin;
								}
									
								if (depth > 2 && (!oneTrue || (landPiece & moveBoard_p1_card1_rev[pp])))
									if ((targetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].load(std::memory_order_relaxed) & (1ULL << (STORE_WIN ? 32 : 0) << (ti.pieceIndex % NUM_BOARDS_PER_U64))) == 0)
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
					newP1Wins |= 1ULL << bitIndex;

					
					{ // also mark all states with p0 to move that have the option of moving to this board
						U64 pk1 = _tzcnt_u64(board.bbk[1]);
						U64 pk1Unmove0 = moveBoard_p0_card1side_rev[pk1];
						U64 pk1Unmove1 = moveBoard_p0_card0side_rev[pk1];
						U64 bbk0WinPosUnmove0 = moveBoard_p0_card1side_rev[PTEMPLE[0]];
						U64 bbk0WinPosUnmove1 = moveBoard_p0_card0side_rev[PTEMPLE[0]];

						bool kingInTempleRange0 = bbk0WinPosUnmove0 & board.bbk[0];
						bool kingInTempleRange1 = bbk0WinPosUnmove1 & board.bbk[0];
						bool pawnTempleBlock = board.bbp[0] & (1 << PTEMPLE[0]);
								
						U64 scan = board.bbp[0] & ~board.bbk[0]; // no reverse take moves
						while (scan) { // pawn unmoves
							U64 sourcePiece = scan & -scan;
							U64 bbp = board.bbp[0] & ~sourcePiece;
							U64 pp = _tzcnt_u64(sourcePiece);
							U64 land = moveboard_p0_cardside_rev[pp] & ~board.bbp[1] & ~board.bbp[0];
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
									BoardToIndexIntermediate im;
									auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card1side_rev, im);
									p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
									if (!isWinInOne1) {
										auto ti1 = boardToIndexFromIntermediate<0>(targetBoard, moveBoard_p0_card0side_rev, ti, im);
										p0ReverseTargetRow1[ti1.pieceCnt_kingsIndex][ti1.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti1.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
									}
								} else if (!isWinInOne1) {
									auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card0side_rev);
									p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
								}
							}
							scan &= scan - 1;
						}
						{ // king unmove
							U64 sourcePiece = board.bbk[0];
							U64 bbp = board.bbp[0] & ~sourcePiece;
							U64 pp = _tzcnt_u64(sourcePiece);
							U64 land = moveboard_p0_cardside_rev[pp] & ~board.bbp[1] & ~board.bbp[0] & ~(1 << PTEMPLE[0]);
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
									BoardToIndexIntermediate im;
									auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card1side_rev, im);
									p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
									if (!isWinInOne1) {
										auto ti1 = boardToIndexFromIntermediate<0>(targetBoard, moveBoard_p0_card0side_rev, ti, im);
										p0ReverseTargetRow1[ti1.pieceCnt_kingsIndex][ti1.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti1.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
									}
								} else if (!isWinInOne1) {
									auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card0side_rev);
									p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
								}
							}
						}
						
						
						if (_popcnt64(board.bbp[1]) < TB_MEN / 2) { // reverse take move
							U64 scan = board.bbp[0] & ~board.bbk[0];
							while (scan) { // pawn unmoves
								U64 sourcePiece = scan & -scan;
								U64 bbp = board.bbp[0] & ~sourcePiece;
								U64 bbp1 = board.bbp[1] | sourcePiece;
								U64 pp = _tzcnt_u64(sourcePiece);
								U64 land = moveboard_p0_cardside_rev[pp] & ~board.bbp[1] & ~board.bbp[0];
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
										BoardToIndexIntermediate im;
										auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card1side_rev, im);
										p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
										if (!isWinInOne1) {
											auto ti1 = boardToIndexFromIntermediate<0>(targetBoard, moveBoard_p0_card0side_rev, ti, im);
											p0ReverseTargetRow1[ti1.pieceCnt_kingsIndex][ti1.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti1.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
										}
									} else if (!isWinInOne1) {
										auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card0side_rev);
										p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
									}
								}
								scan &= scan - 1;
							}
							{ // king unmove
								U64 sourcePiece = board.bbk[0];
								U64 bbp = board.bbp[0] & ~sourcePiece;
								U64 bbp1 = board.bbp[1] | sourcePiece;
								U64 pp = _tzcnt_u64(sourcePiece);
								U64 land = moveboard_p0_cardside_rev[pp] & ~board.bbp[1] & ~board.bbp[0] & ~(1 << PTEMPLE[0]);
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
										BoardToIndexIntermediate im;
										auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card1side_rev, im);
										p0ReverseTargetRow0[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
										if (!isWinInOne1) {
											auto ti1 = boardToIndexFromIntermediate<0>(targetBoard, moveBoard_p0_card0side_rev, ti, im);
											p0ReverseTargetRow1[ti1.pieceCnt_kingsIndex][ti1.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti1.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
										}
									} else if (!isWinInOne1) {
										auto ti = boardToIndex<0>(targetBoard, moveBoard_p0_card0side_rev);
										p0ReverseTargetRow1[ti.pieceCnt_kingsIndex][ti.pieceIndex / NUM_BOARDS_PER_U64].fetch_or(getWinBits(ti.pieceIndex % NUM_BOARDS_PER_U64), std::memory_order_relaxed);
									}
								}
							}
						}
					}
					// win = 1;
					// U64 cardI=0x10 board.bbp[0]=0x1000300 board.bbp[1]=0x80410 110
					// std::cout << std::hex << cardI << ' ' << board.bbp[0] << ' ' << board.bbp[1] << ' ' << (board.bbk[0] | board.bbk[1]) << std::endl;
				notWin:;
					//  if (i >= 149035927) {
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
	}
	if (mod)
		modified = true;
}