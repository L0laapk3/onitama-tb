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
constexpr U64 NUM_CHUNKS = 8192;


// you can use a vector of atomics but you cant perform any actions that rely on its members being movable, such as resizing it, push_back etc.
// see https://www.reddit.com/r/Cplusplus/comments/2hvqhe/stdvectorstdatomicintvector_size_t_fails_on_clang/
typedef std::vector<std::atomic<U64>> TableBaseRow;
typedef std::array<TableBaseRow, 30> TableBase;


void generateFirstWins(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter);
void singleDepthPass(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter);

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

		atomic_thread_fence(std::memory_order_release);
		std::atomic<U64> chunkCounter = 0;
		for (U64 i = 0; i < numThreads; i++)
			threads[i] = std::thread(depth == 1 ? &generateFirstWins : &singleDepthPass, std::ref(cards), std::ref(tb), std::ref(chunkCounter));
		for (auto& thread : threads)
			thread.join();
		atomic_thread_fence(std::memory_order_acquire);

		const float time = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count()) / 1000000;
		totalTime += time;
		U64 cnt = cnt_0;
		for (auto& row : tb)
			for (auto& val : row)
				cnt += _popcnt32(val);
		if (cnt == totalBoards)
			break;
		cnt -= totalBoards;
		totalBoards += cnt;
		printf("depth %3llu: %10llu boards in %.3fs\n", depth, cnt, time);
		depth++;
	}
	const float totalInclusiveTime = std::max<float>(1, (U64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginLoopTime).count()) / 1000000;
	printf("found %12llu boards in %.3fs/%.3fs\n", totalBoards, totalTime, totalInclusiveTime);

	return tb;
}





// marks all p0 to move win in 1 states.
void generateFirstWins(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter) {
	while (true) {
		U64 currChunk = chunkCounter++;
		if (currChunk >= NUM_CHUNKS)
			return;

		for (U64 cardI = 0; cardI < 30; cardI++) { // naive way
			auto permutation = CARDS_PERMUTATIONS[cardI];
			MoveBoard combinedMoveBoardFlip = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
			// std::cout << cardI << " " << (U32)permutation.playerCards[0][0] << " " << (U32)permutation.playerCards[0][1] << std::endl;
			// print(combinedMoveBoardReverse);

			auto& row = tb[cardI];
			for (U64 index = row.size() * currChunk / NUM_CHUNKS * 32; index < std::min(row.size() * (currChunk + 1) / NUM_CHUNKS * 32, TB_ROW_SIZE); index++) {
				auto board = indexToBoard<false>(index);

				if (board.isWinInOne<false>(combinedMoveBoardFlip)) {
					// std::cout << index << " " << cardI << " " << (U32)permutation.playerCards[0][0] << " " << (U32)permutation.playerCards[0][1] << " ";
					// board.print();
					row[index / 32].fetch_or(0x100000001ULL << (index % 32), std::memory_order_relaxed);
				}

				if (0) {
					auto invBoard = indexToBoard<true>(index);
					auto invCardI = CARDS_INVERT[cardI];
					auto invPermutation = CARDS_PERMUTATIONS[invCardI];
					MoveBoard invCombinedMoveBoardFlip = combineMoveBoards(cards.moveBoardsForward[invPermutation.playerCards[1][0]], cards.moveBoardsForward[invPermutation.playerCards[1][1]]);
					if (board.isWinInOne<false>(combinedMoveBoardFlip) != invBoard.isWinInOne<true>(invCombinedMoveBoardFlip)) {
						print(combinedMoveBoardFlip);
						board.print();
						invBoard.print();
						std::cout << board.isWinInOne<false>(combinedMoveBoardFlip) << ' ' << invBoard.isWinInOne<true>(invCombinedMoveBoardFlip) << std::endl;
						board.isWinInOne<false>(combinedMoveBoardFlip);
						invBoard.isWinInOne<true>(invCombinedMoveBoardFlip);
						assert(false);
					}
				}
			}
		}
	}
}


// iterate over unresolved states to find p0 wins with p1 to move. Check if all possible p1 moves result in wins for p0.
void singleDepthPass(const CardsInfo& cards, TableBase& tb, std::atomic<U64>& chunkCounter) {
	while (true) {
		U64 currChunk = chunkCounter++;
		if (currChunk >= NUM_CHUNKS)
			return;
		// check if all P1 moves lead to a victory
		for (U64 cardI = 0; cardI < 30; cardI++) { // naive way

			U64 invCardI = CARDS_INVERT[cardI];
			auto& row = tb[invCardI];
			auto permutation = CARDS_PERMUTATIONS[cardI];
			// forward moves for p1 so reverse moveboards
			const std::array<const MoveBoard*, 2> moveBoards{ &cards.moveBoardsReverse[permutation.playerCards[1][0]], &cards.moveBoardsReverse[permutation.playerCards[1][1]] };
			MoveBoard invCombinedMoveBoardFlip = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[1][0]], cards.moveBoardsForward[permutation.playerCards[1][1]]);
			const std::array<TableBaseRow*, 2> targetRows{ &tb[CARDS_SWAP[cardI][1][0]], &tb[CARDS_SWAP[cardI][1][1]] };

			// moveboard for reversing p0
			const MoveBoard& p0ReverseMoveBoard = cards.moveBoardsReverse[permutation.sideCard];
			const std::array<TableBaseRow*, 2> p0ReverseTargetRows{ &tb[CARDS_SWAP[cardI][0][0]], &tb[CARDS_SWAP[cardI][0][1]] };

			for (U64 tbIndex = row.size() * currChunk / NUM_CHUNKS; tbIndex < row.size() * (currChunk + 1) / NUM_CHUNKS; tbIndex++) {
				auto& entry = row[tbIndex];
				for (U32 bits = ~entry; bits; bits &= bits - 1) {
					U64 bitIndex = _tzcnt_u64(bits);
					Board board = indexToBoard<true>(tbIndex * 32 + bitIndex); // inverted because index assumes p0 to move and we are looking for the board with p1 to move
					{
						assert(!board.isWinInOne<true>(invCombinedMoveBoardFlip));

						U64 scan = board.bbp[1];
						while (scan) {
							U64 sourcePiece = scan & -scan;
							scan &= scan - 1;
							U64 bbp = board.bbp[1] - sourcePiece;
							U64 pp = _tzcnt_u64(sourcePiece);
							for (U64 cardSelect = 0; cardSelect < 2; cardSelect++) {
								const MoveBoard& moveBoard = *moveBoards[cardSelect];
								U64 land = moveBoard[pp] & ~board.bbp[1];
								const TableBaseRow& targetRow = *targetRows[cardSelect];
								while (land) {
									U64 landPiece = land & -land;
									assert((landPiece & board.bbk[0]) == 0);
									land &= land - 1;
									Board targetBoard{
										.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
										.bbk = { board.bbk[0], sourcePiece == board.bbk[1] ? landPiece : board.bbk[1] },
									};

									assert(targetBoard.bbk[1] != 1 << PTEMPLE[1]);
									U64 targetIndex = boardToIndex<false>(targetBoard); // the resulting board has p0 to move and needs to be a win
									if ((targetRow[targetIndex / 32].load(std::memory_order_relaxed) & (1ULL << (targetIndex % 32))) == 0)
										goto notWin;
								}
							}
						}
					}

					// all p1 moves result in win for p0. mark state as won for p0
					entry.fetch_or(0x100000001ULL << bitIndex, std::memory_order_relaxed);
					// also mark all states with p0 to move that have the option of moving to this board
					{
						bool uncaptureAllowed = _popcnt64(board.bbp[1]) < TB_MEN / 2; // todo: template this?
						U64 scan = board.bbp[0];
						while (scan) {
							U64 sourcePiece = scan & -scan;
							bool kingMove = sourcePiece == board.bbk[0];
							scan &= scan - 1;
							U64 bbp = board.bbp[0] - sourcePiece;
							U64 pp = _tzcnt_u64(sourcePiece);
							U64 land = p0ReverseMoveBoard[pp] & ~board.bbp[1] & ~board.bbp[0];
							if (kingMove)
								land &= ~(1 << PTEMPLE[0]);
							while (land) {
								U64 landPiece = land & -land;
								land &= land - 1;
#pragma unroll
								for (U64 uncapture = 0; uncapture < 1 + uncaptureAllowed; uncapture++) {
									Board targetBoard{
										.bbp = { bbp | landPiece, board.bbp[1] | (uncapture ? sourcePiece : 0) },
										.bbk = { kingMove ? landPiece : board.bbk[0], board.bbk[1] },
									};
									U64 targetIndex = boardToIndex<false>(targetBoard);
#pragma unroll
									for (U64 cardSelect = 0; cardSelect < 2; cardSelect++) {
										// i++;
										(*p0ReverseTargetRows[cardSelect])[targetIndex / 32].fetch_or(0x100000001ULL << (targetIndex % 32), std::memory_order_relaxed);
									}
								}
							}
						}
					}

				notWin:;
				}
			}
		}
	}
}