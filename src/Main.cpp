#include "Card.hpp"
#include "TableBase.h"

#include <iostream>
#include <fstream>
#include <string>



int main(int, char**) {



	const CardsInfo CARDS{ BOAR, OX, ELEPHANT, HORSE, CRAB }; // perft cards
	// const CardsInfo CARDS{ CRAB, DRAGON, ELEPHANT, GOOSE, HORSE }; // smallest TB
	// const CardsInfo CARDS{ FROG, MANTIS, MONKEY, RABBIT, TIGER }; //largest TB

    if (0) {
        testIndexing(CARDS);
    } else if (0) {
		exhaustiveIndexTest(CARDS);
	} else if (0) {
        auto tb = generateTB(CARDS);
		tb->testCompression();
	} else if (1) {
        auto tb = generateTB(CARDS);
		auto tbBinary = tb->compress();
		std::ofstream f(std::to_string(TB_MEN) + "men.bin", std::ios::binary);
		f.write(reinterpret_cast<char*>(tbBinary.data()), tbBinary.size());
		f.close();
    } else {

		U64 smallestTB = (U64)-1, largestTB = 0;
		std::array<U32, 5> smallestIndexes, largestIndexes;
		iterateCardCombinations([&](const CardsInfo& cards, const std::array<U32, 5>& cardsIndexes) {
			U64 totalRows = 0;
			for (U64 cardI = 0; cardI < CARDSMULT; cardI++) {
				auto permutation = CARDS_PERMUTATIONS[cardI];
				const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);

				iterateTBCounts(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
					totalRows += (rowSize + NUM_BOARDS_PER_U64 - 1) / NUM_BOARDS_PER_U64;
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
		
			
		std::cout << "smallest tb size (" << smallestTB * sizeof(U64) / 1024 / 1024 << "MB): ";
		std::cout << smallestIndexes[0] << ' ' << smallestIndexes[1] << ' ' << smallestIndexes[2] << ' ' << smallestIndexes[3] << ' ' << smallestIndexes[4] << std::endl;
		std::cout << "largest tb size (" << largestTB * sizeof(U64) / 1024 / 1024 << "MB): ";
		std::cout << largestIndexes[0] << ' ' << largestIndexes[1] << ' ' << largestIndexes[2] << ' ' << largestIndexes[3] << ' ' << largestIndexes[4] << std::endl;
	}
}
