#pragma once

#include "Types.h"
#include "Card.hpp"

#include <array>
#include <x86intrin.h>
#include <xmmintrin.h>



constexpr std::array<U64, 2> PTEMPLE = { 22, 2 };

class Board {
public:
	std::array<U64, 2> bbp;
	std::array<U64, 2> bbk;

	template <U64 player>
	bool inline isWinInOne(const MoveBoard& inverseMovement);

	// debug utils
	void print() const;
	Board invert() const;
};



template <U64 player>
bool inline Board::isWinInOne(const MoveBoard& reverseMoveBoard) {
	if ((reverseMoveBoard[PTEMPLE[player]] & bbk[player]) && !(bbp[player] & (1 << PTEMPLE[player])))
		return true;
	U64 pk = _tzcnt_u64(bbk[1-player]);
	//std::cout << std::bitset<25>(reverseMoveBoard[pk]) << ' ' << std::bitset<25>(bbp[player]) << std::endl;
	return reverseMoveBoard[pk] & bbp[player];
}

template bool Board::isWinInOne<0>(const MoveBoard& reverseMoveBoard);
template bool Board::isWinInOne<1>(const MoveBoard& reverseMoveBoard);