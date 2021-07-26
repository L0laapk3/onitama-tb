
#include "Board.h"
#include "x86intrin.h"
#include <xmmintrin.h>
#include <iostream>




template <U64 player>
bool Board::isWinInOne(const MoveBoard& reverseMoveBoard) {
	if ((reverseMoveBoard[PTEMPLE[player]] & bbk[player]) && !(bbp[player] & (1 << PTEMPLE[player])))
		return true;
	U64 pk = _tzcnt_u64(bbk[1-player]);
	return reverseMoveBoard[pk] & bbp[player];
}

template bool Board::isWinInOne<0>(const MoveBoard& reverseMoveBoard);
template bool Board::isWinInOne<1>(const MoveBoard& reverseMoveBoard);



void Board::print() {
	for (int r = 5; r-- > 0;) {
		for (int c = 0; c < 5; c++) {
			const int mask = 1 << (5 * r + c);
			if (bbp[0] & mask) {
				if ((bbp[1] | bbk[1]) & mask)    std::cout << '?';
				else if (bbk[0] & mask)          std::cout << '0';
				else                             std::cout << 'o';
			} else if (bbp[1] & mask) {
				if (bbk[1] & mask)               std::cout << 'X';
				else                             std::cout << '+';
			} else if ((bbk[0] | bbk[1]) & mask) std::cout << '?';
			else                                 std::cout << '.';
		}
		std::cout << std::endl;
	}
	std::cout << std::endl;
}