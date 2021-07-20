#pragma once

#include "Types.h"

#include <array>

class Board {
public:
	U64 bbp0;
	U64 bbp1;
	U64 bbk0;
	U64 bbk1;

	template <bool invert>
	static U64 toIndex(const Board& board);
	template <bool invert>
	static Board fromIndex(U64 index);
};
