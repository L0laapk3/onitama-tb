#pragma once

#include "Types.h"
#include "Card.hpp"

#include <array>



constexpr std::array<U64, 2> PTEMPLE = { 2, 22 };

class Board {
public:
	std::array<U64, 2> bbp;
	std::array<U64, 2> bbk;

	template <U64 player>
	bool isWinInOne(const MoveBoard& inverseMovement);
};
