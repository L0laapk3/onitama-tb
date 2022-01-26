#include "Card.hpp"
#include "TableBase.h"

#include <iostream>



int main(int, char**) {
	const CardsInfo CARDS{ BOAR, OX, ELEPHANT, HORSE, CRAB };

		U64 cardI=0x10;
		Board board{
			{ 0x1000300, 0x80410 },
			{ 0x100, 0x10 },
		};

		auto permutation = CARDS_PERMUTATIONS[cardI]; // cardI = 5: p0: BOAR CRAB, p1: OX ELEPHANT, swap: HORSE
		// forward moves for p1 so reverse moveboards
		const MoveBoard moveBoard_p1_card01_flip = combineMoveBoards(CARDS.moveBoardsForward[permutation.playerCards[1][0]], CARDS.moveBoardsForward[permutation.playerCards[1][1]]);
		
		std::cout << board.isWinInOne<1>(moveBoard_p1_card01_flip) << std::endl;
		auto bi = boardToIndex<1>(board, moveBoard_p1_card01_flip);
		std::cout << bi.pieceCnt_kingsIndex << ' ' << bi.pieceIndex << std::endl;

		auto newBoard = indexToBoard<1>(bi, moveBoard_p1_card01_flip);
		std::cout << std::hex << cardI << ' ' << newBoard.bbp[0] << ' ' << newBoard.bbp[1] << ' ' << (newBoard.bbk[0] | newBoard.bbk[1]) << std::dec << std::endl;

    if (0) {
        testIndexing(CARDS);
        return 0;
    } else if (0) {
		exhaustiveIndexTest(CARDS);
	} else {
        generateTB(CARDS);
    }
}
