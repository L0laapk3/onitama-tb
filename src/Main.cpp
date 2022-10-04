#include "Card.hpp"
#include "TableBase.h"

#include <iostream>
#include <fstream>
#include <string>
#include <future>
#include <iterator>


int main(int, char**) {

	constexpr uint8_t TB_MEN = 6;
	constexpr U64 MEM_LIMIT = 100ULL << 30; // 30GB

	const CardsInfo CARDS{ BOAR, OX, ELEPHANT, HORSE, CRAB }; // perft cards
	// const CardsInfo CARDS{ CRAB, DRAGON, ELEPHANT, GOOSE, HORSE }; // smallest TB
	// const CardsInfo CARDS{ FROG, MANTIS, MONKEY, RABBIT, TIGER }; //largest TB

	try {
		if (0) {
			testIndexing(CARDS);
		} else if (0) {
			exhaustiveIndexTest(CARDS);
		} else if (1) {
			auto tb = TableBase<TB_MEN, false>::generate(CARDS, MEM_LIMIT); // 100 GB

			if (1) {
				std::ofstream f(std::to_string(TB_MEN) + "men_draws_sparse.bin", std::ios::binary);
				TableBase<TB_MEN, false>::storeSparse(f, *tb, CARDS);
				f.close();
				std::cout << "saved result" << std::endl;
			}

			if (1) {
				std::ifstream f(std::to_string(TB_MEN) + "men_draws_sparse.bin", std::ios::in | std::ios::binary);
				auto tbFile = TableBase<TB_MEN, false>::loadSparse(f, CARDS, MEM_LIMIT); // 100 GB
				f.close();

				for (U64 i = 0; i < tb->refTable.size(); i++)
					for (U64 j = 0; j < tb->refTable[i].mem.size(); j++)
						if (tb->refTable[i].mem[j] != ~tbFile->refTable[i].mem[j]) {
							std::cout << "ERROR: " << i << " " << j << std::endl;
							std::cout << tb->refTable[i].mem[j] << " " << tbFile->refTable[i].mem[j] << std::endl;
						}
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
						totalRows += (rowSize + NUM_BOARDS_PER_U64<false> - 1) / NUM_BOARDS_PER_U64<false>;
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
		
	} catch(const std::exception& e) {
		std::cout << e.what() << std::endl << std::flush;
	}

	
	// std::promise<void>().get_future().wait(); // sleep forever
}
