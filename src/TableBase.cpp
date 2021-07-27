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


constexpr bool COUNT_BOARDS = true;


typedef std::vector<std::atomic<U64>> TableBaseRow;
typedef std::array<TableBaseRow, 30> TableBase;


void generateFirstWins(const CardsInfo& cards, TableBase& tb, U64 thisThread, U64 numThreads);

TableBase generateTB(const CardsInfo& cards) {

	// you can use a vector of atomics but you perform any actions that rely on its members being movable, such as resizing it, push_back etc.
	// see https://www.reddit.com/r/Cplusplus/comments/2hvqhe/stdvectorstdatomicintvector_size_t_fails_on_clang/
	TableBase tb;
	for (auto& row : tb)
		row = TableBaseRow((TB_ROW_SIZE + 31) / 32);

	const U64 numThreads = std::clamp<U64>(std::thread::hardware_concurrency(), 1, 1024);
	std::vector<std::thread> threads(numThreads);

	U64 depth = 1;
	auto beginTime = std::chrono::steady_clock::now();

	atomic_thread_fence(std::memory_order_release);
	for (U64 i = 0; i < numThreads; i++)
		threads[i] = std::thread(&generateFirstWins, std::ref(cards), std::ref(tb), i, numThreads);
	for (auto& thread : threads)
		thread.join();
	atomic_thread_fence(std::memory_order_acquire);

	const auto time = std::max(1ULL, (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count());
	if (COUNT_BOARDS) {
		U64 cnt = 0;
		for (auto& row : tb)
			for (auto& val : row)
				cnt += _popcnt32(val);

		printf("depth %3llu: %10llu boards in %.3fs\n", depth, cnt, (float)time / 1000000);
	} else
		printf("depth %3llu: took %.3fs\n", depth, (float)time / 1000000);

	return tb;
}






void generateFirstWins(const CardsInfo& cards, TableBase& tb, U64 thisThread, U64 numThreads) {
	for (U64 cardI = 0; cardI < 30; cardI++) { // naive way
		auto permutation = CARDS_PERMUTATIONS[cardI];
		MoveBoard combinedMoveBoardReverse = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		// std::cout << cardI << " " << (U32)permutation.playerCards[0][0] << " " << (U32)permutation.playerCards[0][1] << std::endl;
		// print(combinedMoveBoardReverse);

		auto& row = tb[cardI];
		for (U64 index = TB_ROW_SIZE * thisThread / numThreads; index < TB_ROW_SIZE * (thisThread + 1) / numThreads; index++) {
			auto board = indexToBoard<false>(index);

			// if (_popcnt64(board.bbp[0]) > 2 || _popcnt64(board.bbp[1]) > 2)
			// 	continue;

			if (board.isWinInOne<false>(combinedMoveBoardReverse)) {
				// std::cout << index << " " << cardI << " " << (U32)permutation.playerCards[0][0] << " " << (U32)permutation.playerCards[0][1] << " ";
				// board.print();
				row[index / 32].fetch_or(1ULL << (index % 32), std::memory_order_relaxed);
			}
		}
	}
}