#include "TableBase.h"

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <vector>
#include <atomic>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iostream>
#include <cassert>
#include <immintrin.h>
#include <x86intrin.h>


constexpr bool COUNT_BOARDS = true;


constexpr U64 NUM_CHUNKS_PER_CARD = TB_ROW_SIZE / 4096;
constexpr U64 NUM_CHUNKS_PER_PAIR = NUM_CHUNKS_PER_CARD * 3;
constexpr U64 NUM_CHUNKS = NUM_CHUNKS_PER_CARD * 30;


// you can use a vector of atomics but you cant perform any actions that rely on its members being movable, such as resizing it, push_back etc.
// see https://www.reddit.com/r/Cplusplus/comments/2hvqhe/stdvectorstdatomicintvector_size_t_fails_on_clang/
typedef std::vector<std::atomic<U64>> TableBaseRow;
typedef std::array<TableBaseRow, 30> TableBase;


void generateFirstWins(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified);
template<bool depth2>
void singleDepthPass(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified);

TableBase generateTB(const CardsInfo& cards) {
	TableBase tb;
	U64 cnt_0 = 0;
	for (auto& row : tb) {
		row = TableBaseRow((TB_ROW_SIZE + 31) / 32);
		row.back() = (1ULL << 32) - (1ULL << (((TB_ROW_SIZE + 31) % 32) + 1)); // mark final rows as resolved so we dont have to worry about it
		cnt_0 -= _popcnt64(row.back());
	}

	const U64 numThreads = std::clamp<U64>(std::thread::hardware_concurrency(), 1, 1024);
	std::vector<std::thread> threads(numThreads);

	U64 depth = 1;
	U64 totalBoards = 0;
	float totalTime = 0;
	auto beginLoopTime = std::chrono::steady_clock::now();
	while (true) {
		auto beginTime = std::chrono::steady_clock::now();

		bool modified = false;
		std::atomic<U64> chunkCounter = 0;
		for (U64 i = 0; i < numThreads; i++)
			threads[i] = std::thread(depth == 1 ? &generateFirstWins : depth == 2 ? &singleDepthPass<true> : &singleDepthPass<false>, std::ref(cards), std::ref(tb), std::ref(chunkCounter), std::ref(modified));
		for (auto& thread : threads)
			thread.join();

		const float time = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count()) / 1000000;
		totalTime += time;
		U64 cnt = cnt_0;
		if (COUNT_BOARDS || !modified) {
			for (auto& row : tb)
				for (auto& val : row)
					cnt += _popcnt32(val);
			cnt -= totalBoards;
			totalBoards += cnt;
		}
		if (!modified)
			break;
		if (COUNT_BOARDS)
			printf("iter %3llu: %11llu boards in %.3fs\n", depth, cnt, time);
		else
			printf("iter %3llu in %.3fs\n", depth, time);
		depth++;
	}
	const float totalInclusiveTime = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginLoopTime).count()) / 1000000;
	printf("found %llu boards in %.3fs/%.3fs\n", totalBoards, totalTime, totalInclusiveTime);

	return tb;
}





// marks all p0 to move win in 1 states.
void generateFirstWins(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified) {
	modified = true;
	while (true) {
		U64 currChunk = chunkCounter.fetch_add(1, std::memory_order_relaxed);
		if (currChunk >= NUM_CHUNKS)
			break;

		U64 pairI = 10 * currChunk / NUM_CHUNKS;
		U64 cardI = CARDS_P0_PAIRS[pairI];

		auto permutation = CARDS_PERMUTATIONS[cardI];
		MoveBoard combinedMoveBoardFlip = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		// std::cout << cardI << " " << (U32)permutation.playerCards[0][0] << " " << (U32)permutation.playerCards[0][1] << std::endl;
		// print(combinedMoveBoardReverse);

		TableBaseRow& row0 = tb[cardI];
		TableBaseRow& row1 = tb[CARDS_SWAP[cardI][1][0]];
		TableBaseRow& row2 = tb[CARDS_SWAP[cardI][1][1]];
		for (U64 tbIndex = (TB_ROW_SIZE + 31) / 32 * (currChunk % NUM_CHUNKS_PER_PAIR) / NUM_CHUNKS_PER_PAIR; tbIndex < (TB_ROW_SIZE + 31) / 32 * ((currChunk % NUM_CHUNKS_PER_PAIR) + 1) / NUM_CHUNKS_PER_PAIR; tbIndex++) {
			__builtin_prefetch(&row0[tbIndex]);
			__builtin_prefetch(&row1[tbIndex]);
			__builtin_prefetch(&row2[tbIndex]);
			U64 bits = 0;

			if (tbIndex < (TB_ROW_SIZE + 31) / 32 - 1)
				#pragma unroll
				for (U64 bitIndex = 0; bitIndex < 32; bitIndex++) {
					auto board = indexToBoard<false>(tbIndex * 32 + bitIndex);
					if (board.isWinInOne<false>(combinedMoveBoardFlip))
						bits |= 0x100000001ULL << bitIndex;
				}
			else
				for (U64 bitIndex = 0; bitIndex < ((TB_ROW_SIZE + 31) % 32) + 1; bitIndex++) {
					auto board = indexToBoard<false>(tbIndex * 32 + bitIndex);
					if (board.isWinInOne<false>(combinedMoveBoardFlip))
						bits |= 0x100000001ULL << bitIndex;
				}
			row0[tbIndex].fetch_or(bits, std::memory_order_relaxed);
			row1[tbIndex].fetch_or(bits, std::memory_order_relaxed);
			row2[tbIndex].fetch_or(bits, std::memory_order_relaxed);
		}
	}
}



// iterate over unresolved states to find p0 wins with p1 to move. Check if all possible p1 moves result in wins for p0.
template<bool depth2>
void singleDepthPass(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter, bool& modified) {
	bool mod = false;
	while (true) {
		U64 currChunk = chunkCounter++;
		if (currChunk >= NUM_CHUNKS)
			break;
		// check if all P1 moves lead to a victory
		U64 cardI = 30 * currChunk / NUM_CHUNKS;

		U64 invCardI = CARDS_INVERT[cardI];
		auto& row = tb[invCardI];
		auto permutation = CARDS_PERMUTATIONS[cardI];
		// forward moves for p1 so reverse moveboards
		const MoveBoard& moveBoard0 = cards.moveBoardsReverse[permutation.playerCards[1][0]];
		const MoveBoard& moveBoard1 = cards.moveBoardsReverse[permutation.playerCards[1][1]];
		TableBaseRow& targetRow0 = tb[CARDS_SWAP[cardI][1][0]];
		TableBaseRow& targetRow1 = tb[CARDS_SWAP[cardI][1][1]];
		const MoveBoard combinedMoveBoardFlip = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		
		const MoveBoard combinedMoveBoardsFlipUnmove0 = combineMoveBoards(cards.moveBoardsReverse[permutation.sideCard], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard combinedMoveBoardsFlipUnmove1 = combineMoveBoards(cards.moveBoardsReverse[permutation.sideCard], cards.moveBoardsReverse[permutation.playerCards[0][0]]);

		// moveboard for reversing p0
		const MoveBoard& p0ReverseMoveBoard = cards.moveBoardsReverse[permutation.sideCard];
		TableBaseRow& p0ReverseTargetRow0 = tb[CARDS_SWAP[cardI][0][0]];
		TableBaseRow& p0ReverseTargetRow1 = tb[CARDS_SWAP[cardI][0][1]];

		for (U64 tbIndex = row.size() * (currChunk % NUM_CHUNKS_PER_CARD) / NUM_CHUNKS_PER_CARD; tbIndex < row.size() * ((currChunk % NUM_CHUNKS_PER_CARD) + 1) / NUM_CHUNKS_PER_CARD; tbIndex++) {
			auto& entry = row[tbIndex];
			U64 newP1Wins = 0;
			for (U32 bits = ~entry; bits; bits &= bits - 1) {
				U64 bitIndex = _tzcnt_u64(bits);
				Board board = indexToBoard<true>(tbIndex * 32 + bitIndex); // inverted because index assumes p0 to move and we are looking for the board with p1 to move
				if (!board.isTempleWinInOne<false>(combinedMoveBoardFlip)) {
					U64 kingThreatenPawns = board.isTakeWinInOne<false>(combinedMoveBoardFlip);
					U64 scan = board.bbp[1] & ~board.bbk[1];
					while (scan) { // pawn moves
						U64 sourcePiece = scan & -scan;
						U64 landMask = kingThreatenPawns ? kingThreatenPawns : ~board.bbp[1];
						scan &= scan - 1;
						U64 bbp = board.bbp[1] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						{ // card 1
							U64 land = moveBoard0[pp] & landMask;
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;
								Board targetBoard{
									.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
									.bbk = { board.bbk[0], board.bbk[1] },
								};

								if (!depth2) {
									U64 targetIndex = boardToIndex<false>(targetBoard); // the resulting board has p0 to move and needs to be a win
									if ((targetRow0[targetIndex / 32].load(std::memory_order_relaxed) & (1ULL << (targetIndex % 32))) != 0)
										continue;
								}
								
								goto notWin;
							}
						}
						{ // card 2
							U64 land = moveBoard1[pp] & landMask;
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;
								Board targetBoard{
									.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
									.bbk = { board.bbk[0], board.bbk[1] },
								};

								if (!depth2) {
									U64 targetIndex = boardToIndex<false>(targetBoard); // the resulting board has p0 to move and needs to be a win
									if ((targetRow1[targetIndex / 32].load(std::memory_order_relaxed) & (1ULL << (targetIndex % 32))) != 0)
										continue;
								}
								
								goto notWin;
							}
						}
					}
					{ // king move
						U64 sourcePiece = board.bbk[1];
						U64 landMask = ~board.bbp[1];
						U64 bbp = board.bbp[1] - sourcePiece;
						U64 pp = _tzcnt_u64(sourcePiece);
						{ // card 1
							U64 land = moveBoard0[pp] & landMask;
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;
								Board targetBoard{
									.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
									.bbk = { board.bbk[0], landPiece },
								};

								if (!targetBoard.isTakeWinInOne<false>(combinedMoveBoardFlip)) {
									if (!depth2) {
										U64 targetIndex = boardToIndex<false>(targetBoard); // the resulting board has p0 to move and needs to be a win
										if ((targetRow0[targetIndex / 32].load(std::memory_order_relaxed) & (1ULL << (targetIndex % 32))) != 0)
											continue;
									}
									
									goto notWin;
								}
							}
						}
						{ // card 2
							U64 land = moveBoard1[pp] & landMask;
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;
								Board targetBoard{
									.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
									.bbk = { board.bbk[0], landPiece },
								};

								if (!targetBoard.isTakeWinInOne<false>(combinedMoveBoardFlip)) {

									if (!depth2) {
										U64 targetIndex = boardToIndex<false>(targetBoard); // the resulting board has p0 to move and needs to be a win
										if ((targetRow1[targetIndex / 32].load(std::memory_order_relaxed) & (1ULL << (targetIndex % 32))) != 0)
											continue;
									}
									
									goto notWin;
								}
							}
						}
					}
				}

				// all p1 moves result in win for p0. mark state as won for p0
				newP1Wins |= 0x000000001ULL << bitIndex;

				
				// also mark all states with p0 to move that have the option of moving to this board
				
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
					scan &= scan - 1;
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
							U64 targetIndex = boardToIndex<false>(targetBoard);
							p0ReverseTargetRow0[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
							if (!isWinInOne1)
								p0ReverseTargetRow1[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
						} else {
							if (!isWinInOne1) {
								U64 targetIndex = boardToIndex<false>(targetBoard);
								p0ReverseTargetRow1[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
							}

						}
					}
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
							U64 targetIndex = boardToIndex<false>(targetBoard);
							p0ReverseTargetRow0[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
							if (!isWinInOne1)
								p0ReverseTargetRow1[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
						} else {
							if (!isWinInOne1) {
								U64 targetIndex = boardToIndex<false>(targetBoard);
								p0ReverseTargetRow1[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
							}

						}
					}
				}
				
				
				if (_popcnt64(board.bbp[1]) < TB_MEN / 2) { // reverse take move
					scan = board.bbp[0] & ~board.bbk[0];
					while (scan) { // pawn unmoves
						U64 sourcePiece = scan & -scan;
						scan &= scan - 1;
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
								U64 targetIndex = boardToIndex<false>(targetBoard);
								p0ReverseTargetRow0[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
								if (!isWinInOne1)
									p0ReverseTargetRow1[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
							} else {
								if (!isWinInOne1) {
									U64 targetIndex = boardToIndex<false>(targetBoard);
									p0ReverseTargetRow1[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
								}

							}
						}
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
								U64 targetIndex = boardToIndex<false>(targetBoard);
								p0ReverseTargetRow0[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
								if (!isWinInOne1)
									p0ReverseTargetRow1[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
							} else {
								if (!isWinInOne1) {
									U64 targetIndex = boardToIndex<false>(targetBoard);
									p0ReverseTargetRow1[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
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