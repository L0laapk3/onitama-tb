
#include "Board.h"
#include "x86intrin.h"
#include <xmmintrin.h>





template <U64 player>
bool Board::isWinInOne(const MoveBoard& reverseMoveBoard) {
	if (reverseMoveBoard[PTEMPLE[player]] & bbk[player])
		return true;
	U64 pk = _tzcnt_u64(bbk[1-player]);
	return reverseMoveBoard[pk] & bbp[player];
}

template bool Board::isWinInOne<0>(const MoveBoard& reverseMoveBoard);
template bool Board::isWinInOne<1>(const MoveBoard& reverseMoveBoard);