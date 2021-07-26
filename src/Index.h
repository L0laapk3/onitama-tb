#pragma once

#include "Types.h"
#include "Helper.h"
#include "Board.h"

#include <array>
#include <tuple>


#define TB_MEN 6



constexpr U64 KINGSMULT = 24 + 23*23;

constexpr auto OFFSET_ORDER = []() {
	std::array<std::pair<U64, U64>, TB_MEN/2 * TB_MEN/2> a;
    int index = 0;
	for (int i = TB_MEN - 1; i-- > 0; )
        for (int j = i % 2; j <= TB_MEN; j += 2)
            for (int k = -1; k <= (j == 0 ? 0 : 1); k += 2)
                if (i - j >= 0 && i + j <= TB_MEN - 2) {
                    int p0c = (i - k * j) / 2, p1c = (i + k * j) / 2;
                    a[index++] = { p0c, p1c };
                }
	// for (int p0c = TB_MEN/2-1; p0c --> 0; )
	// 	for (int p1c = TB_MEN/2-1; p1c --> 0; )
	// 		a[index++] = { p0c, p1c };
    return a;
}();
template<bool includeSelf>
constexpr auto GENERATE_TABLE_SIZES() {
	std::array<std::array<U64, TB_MEN/2>, TB_MEN/2> a;
    U64 offset_cumul = 0;
	for (auto& pc : OFFSET_ORDER) {
		if (!includeSelf) a[pc.first][pc.second] = offset_cumul;
        offset_cumul += KINGSMULT * (fact(23, 23-pc.first) / fact(pc.first)) * (fact(23-pc.first, 23-pc.first-pc.second) / fact(pc.second));
		if (includeSelf) a[pc.first][pc.second] = offset_cumul;
    }
	return std::pair{ a, offset_cumul };
}
constexpr U64 TB_ROW_SIZE = GENERATE_TABLE_SIZES<false>().second;




template <bool invert>
Board indexToBoard(U64 index);

template <bool invert>
U64 boardToIndex(const Board& board);

void testIndexing();