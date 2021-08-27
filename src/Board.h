#pragma once

#include "Types.h"
#include "Card.hpp"

#include <array>



constexpr std::array<U64, 2> PTEMPLE = { 22, 2 };

class Board {
public:
	std::array<U64, 2> bbp;
	std::array<U64, 2> bbk;

	template <U64 player>
	bool isWinInOne(const MoveBoard& inverseMovement);

	// debug utils
	void print() const;
	Board invert() const;
};
