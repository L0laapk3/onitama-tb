#include "Card.hpp"
#include "TableBase.h"

#include <iostream>
#include <fstream>
#include <string>
#include <future>


int main(int, char**) {

	constexpr uint8_t TB_MEN = 8;

	const CardsInfo CARDS{ BOAR, OX, ELEPHANT, HORSE, CRAB }; // perft cards
	// const CardsInfo CARDS{ CRAB, DRAGON, ELEPHANT, GOOSE, HORSE }; // smallest TB
	// const CardsInfo CARDS{ FROG, MANTIS, MONKEY, RABBIT, TIGER }; //largest TB


    if (0) {
        testIndexing(CARDS);
    } else if (0) {
		std::cout << "start" << std::endl;
		exhaustiveIndexTest(CARDS);
		std::cout << "done" << std::endl;
	}
	if (1) {
        auto tb = TableBase<TB_MEN, false>::generate(CARDS, 60ULL << 30);

		if (0) {
			auto tbBinary = tb->storeSparse(CARDS);
			std::ofstream f(std::to_string(TB_MEN) + "men_draws_sparse.bin", std::ios::binary);
			f.write(reinterpret_cast<char*>(tbBinary.data()), tbBinary.size() * sizeof(tbBinary[0]));
			f.close();
			std::cout << "saved result" << std::endl;
		}
    } else {

		U64 smallestTB = -1ULL, largestTB = 0;
		std::array<U32, 5> smallestIndexes, largestIndexes;
		iterateCardCombinations([&](const CardsInfo& cards, const std::array<U32, 5>& cardsIndexes) {
			U64 totalRows = 0;
			for (U64 cardI = 0; cardI < CARDSMULT; cardI++) {
				auto permutation = CARDS_PERMUTATIONS[cardI];
				const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);

				iterateTBCounts<6>(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
					totalRows += (rowSize + NUM_BOARDS_PER_ENTRY<false> - 1) / NUM_BOARDS_PER_ENTRY<false>;
				});
			}
			if (totalRows < smallestTB) {
				smallestTB = totalRows;
				smallestIndexes = cardsIndexes;
			}
			if (totalRows > largestTB) {
				largestTB = totalRows;
				largestIndexes = cardsIndexes;
			}
		});


		std::cout << "smallest tb size (" << smallestTB * sizeof(TB_ENTRY) / 1024 / 1024 << "MB): ";
		std::cout << smallestIndexes[0] << ' ' << smallestIndexes[1] << ' ' << smallestIndexes[2] << ' ' << smallestIndexes[3] << ' ' << smallestIndexes[4] << std::endl;
		std::cout << "largest tb size (" << largestTB * sizeof(TB_ENTRY) / 1024 / 1024 << "MB): ";
		std::cout << largestIndexes[0] << ' ' << largestIndexes[1] << ' ' << largestIndexes[2] << ' ' << largestIndexes[3] << ' ' << largestIndexes[4] << std::endl;
	}


	// std::promise<void>().get_future().wait(); // sleep forever
}
