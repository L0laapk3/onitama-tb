#pragma once

#include "Types.h"
#include "Helper.h"
#include "Board.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <tuple>
#include <utility>


struct BoardIndex {
	U32 pieceCnt;
	U32 pieceIndex;
};


constexpr U32 CARDSMULT = 30;
constexpr U32 KINGSMULT = 23*24+1;

template <U16 TB_MEN>
constexpr U32 PIECECOUNTMULT = (TB_MEN / 2) * (TB_MEN / 2);


template <U16 TB_MEN>
constexpr auto TB_MEN_ORDER = []{
	std::array<std::pair<U64, U64>, TB_MEN/2 * TB_MEN/2> a;
    int index = 0;
	for (int i = TB_MEN - 1; i-- > 0; )
		for (int j = i % 2; j <= TB_MEN; j += 2)
			for (int k = -1; k <= (j == 0 ? 0 : 1); k += 2)
				if (i - j >= 0 && i + j <= TB_MEN - 2) {
					int p0c = (i - k * j) / 2, p1c = (i + k * j) / 2;
					a[index++] = { p0c, p1c };
				}
	return a;
}();


// TODO: review llm code: PAWNTABLES
// every placement of PIECES identical pieces on the lowest SQUARES bits, in increasing order
template <U32 SQUARES, U32 PIECES>
constexpr auto PIECE_PLACEMENTS = []{
	std::array<U32, fact(SQUARES, SQUARES - PIECES) / fact(PIECES)> a;
	U32 bb = (1U << PIECES) - 1;
	for (std::size_t i = 0; i < a.size(); i++) {
		a[i] = bb;
		if (i + 1 < a.size()) {
			const U32 t = bb | (bb - 1);
			bb = (t + 1) | (((~t & (t + 1)) - 1) >> (std::countr_zero(bb) + 1));
		}
	}
	return a;
}();

template <U16 TB_MEN, std::size_t ROW>
constexpr auto& PAWNTABLE_P0 = PIECE_PLACEMENTS<25, TB_MEN_ORDER<TB_MEN>[ROW].first + 1>;

// compacted over the 24 - p0c squares not taken by p0: expand with _pdep_u32(bbp1, ~bbp0)
template <U16 TB_MEN, std::size_t ROW>
constexpr auto& PAWNTABLE_P1 = PIECE_PLACEMENTS<24 - TB_MEN_ORDER<TB_MEN>[ROW].first, TB_MEN_ORDER<TB_MEN>[ROW].second + 1>;
