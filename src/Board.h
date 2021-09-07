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
	bool inline isTempleWinInOne(const MoveBoard& inverseMovement);
	template <U64 player>
	U64 inline isTakeWinInOne(const MoveBoard& inverseMovement);
	template <U64 player>
	bool inline isWinInOne(const MoveBoard& inverseMovement);

	// debug utils
	void print() const;
	Board invert() const;
};


template <U64 player>
bool inline Board::isTempleWinInOne(const MoveBoard& reverseMoveBoard) __attribute__((always_inline)) {
	return (reverseMoveBoard[PTEMPLE[player]] & bbk[player]) && !(bbp[player] & (1 << PTEMPLE[player]));
}


template <U64 player>
U64 inline Board::isTakeWinInOne(const MoveBoard& reverseMoveBoard) __attribute__((always_inline)) {
	U64 pk = _tzcnt_u64(bbk[1-player]);
	return reverseMoveBoard[pk] & bbp[player];
}


template <U64 player>
bool inline Board::isWinInOne(const MoveBoard& reverseMoveBoard) __attribute__((always_inline)) {
	if (isTempleWinInOne<player>(reverseMoveBoard))
		return true;
	return isTakeWinInOne<player>(reverseMoveBoard);
}