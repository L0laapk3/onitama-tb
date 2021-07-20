#pragma once

#include "Types.h"


constexpr U64 fact(U64 from, U64 downto = 0) {
	return from <= downto ? 1 : from * fact(from - 1, downto);
}