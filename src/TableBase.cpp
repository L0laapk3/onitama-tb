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


// you can use a vector of atomics but you cant perform any actions that rely on its members being movable, such as resizing it, push_back etc.
// see https://www.reddit.com/r/Cplusplus/comments/2hvqhe/stdvectorstdatomicintvector_size_t_fails_on_clang/
typedef std::vector<std::atomic<U64>> TableBaseRow;
typedef std::array<TableBaseRow, 30> TableBase;


void generateFirstWins(const CardsInfo& cards, TableBase& tb, U64 thisThread, U64 numThreads);
void singleDepthPass(const CardsInfo& cards, TableBase& tb, U64 thisThread, U64 numThreads);

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
		for (U64 i = 0; i < numThreads; i++)
			threads[i] = std::thread(depth == 1 ? &generateFirstWins : &singleDepthPass, std::ref(cards), std::ref(tb), i, numThreads);
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
	printf("finished in %.3fs/%.3fs\n", totalTime, totalInclusiveTime);

	return tb;
}






void generateFirstWins(const CardsInfo& cards, TableBase& tb, U64 thisThread, U64 numThreads) {
	for (U64 cardI = 0; cardI < 30; cardI++) { // naive way
		auto permutation = CARDS_PERMUTATIONS[cardI];
		MoveBoard combinedMoveBoardFLip = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		// std::cout << cardI << " " << (U32)permutation.playerCards[0][0] << " " << (U32)permutation.playerCards[0][1] << std::endl;
		// print(combinedMoveBoardReverse);

		auto& row = tb[cardI];
		for (U64 index = row.size() * thisThread / numThreads * 32; index < std::min(row.size() * (thisThread + 1) / numThreads * 32, TB_ROW_SIZE); index++) {
			auto board = indexToBoard<true>(index);

			if (board.isWinInOne<false>(combinedMoveBoardFLip)) {
				// std::cout << index << " " << cardI << " " << (U32)permutation.playerCards[0][0] << " " << (U32)permutation.playerCards[0][1] << " ";
				// board.print();
				row[index / 32].fetch_or(0x100000001ULL << (index % 32), std::memory_order_relaxed);
			}
		}
	}
}


// 104343780
void singleDepthPass(const CardsInfo& cards, TableBase& tb, U64 thisThread, U64 numThreads) {
	// check if all P1 moves lead to a victory
	for (U64 cardI = 0; cardI < 30; cardI++) { // naive way
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const std::array<const MoveBoard*, 2> moveBoards{ &cards.moveBoardsReverse[permutation.playerCards[1][0]], &cards.moveBoardsReverse[permutation.playerCards[1][1]] };
		const std::array<TableBaseRow*, 2> targetRows{ &tb[CARDS_SWAP[cardI][1][0]], &tb[CARDS_SWAP[cardI][1][1]] };

		auto& row = tb[cardI];
		for (U64 tbIndex = row.size() * thisThread / numThreads; tbIndex < row.size() * (thisThread + 1) / numThreads; tbIndex++) {
			auto& entry = row[tbIndex];
			for (U32 bits = ~entry; bits; bits &= bits-1) {
				U64 bitIndex = _tzcnt_u64(bits);
				Board board = indexToBoard<true>(tbIndex * 32 + bitIndex);
				
				U64 scan = board.bbp[1];
				while (scan) {
					U64 sourcePiece = scan & -scan;
					scan &= scan - 1;
					U64 bbp = board.bbp[1] - sourcePiece;
					U64 pp = _tzcnt_u64(sourcePiece);
					for (U64 cardSelect = 0; cardSelect < 2; cardSelect++) {
						const MoveBoard& moveBoard = *moveBoards[cardSelect];
						const TableBaseRow& targetRow = *targetRows[cardSelect];
						U64 land = moveBoard[pp];
						while(land) {
							U64 landPiece = land & -land;
							assert((landPiece & board.bbk[0]) == 0);
							land &= land - 1;
							Board targetBoard{
								.bbp = { board.bbp[0] & ~landPiece, bbp | landPiece },
								.bbk = { board.bbk[0], sourcePiece == board.bbk[1] ? landPiece : board.bbk[1] },
							};
							U64 targetIndex = boardToIndex<false>(targetBoard);
							if ((targetRow[targetIndex / 32].load(std::memory_order_relaxed) & (1ULL << (targetIndex % 32))) == 0)
								goto notWin;
						}
					}
				}

				row[tbIndex].fetch_or(0x100000001ULL << bitIndex, std::memory_order_relaxed);
				notWin: ;
			}
		}
	}
}