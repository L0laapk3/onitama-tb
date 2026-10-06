module;
#include <immintrin.h>
#include "inline.h"
export module tb:index;
import std;
import :types;
import :helper;

export constexpr U32 CARDSMULT = 30;

export struct PieceCount {
	U16 p0c;
	U16 p1c;
};

export template <U16 TB_MEN>
constexpr auto PIECE_COUNTS = [] {
	std::array<PieceCount, TB_MEN / 2 * TB_MEN / 2> a;
	int index = a.size();
	for (int i = TB_MEN - 1; i-- > 0;)
		for (int j = i % 2; j <= TB_MEN; j += 2)
			for (int k = -1; k <= (j == 0 ? 0 : 1); k += 2)
				if (i - j >= 0 && i + j <= TB_MEN - 2) {
					int p0c = (i - k * j) / 2, p1c = (i + k * j) / 2;
					a[--index] = {static_cast<U16>(p0c + 1), static_cast<U16>(p1c + 1)};
				}
	return a;
}();

// Row with the given piece counts, -1 if it is not part of the table.
export template <U16 TB_MEN>
constexpr int rowIndex(int p0c, int p1c) {
	for (int i = 0; i < static_cast<int>(PIECE_COUNTS<TB_MEN>.size()); i++)
		if (PIECE_COUNTS<TB_MEN>[i].p0c == p0c && PIECE_COUNTS<TB_MEN>[i].p1c == p1c)
			return i;
	return -1;
}

template <U32 SQUARES, U32 PIECES>
constexpr auto PIECE_PLACEMENTS = [] {
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

export template <U16 P0C, U16 P1C>
constexpr auto& PAWNTABLE_P0 = PIECE_PLACEMENTS<25, P0C>;

export template <U16 P0C, U16 P1C>
constexpr auto& PAWNTABLE_P1 = PIECE_PLACEMENTS<25 - P0C, P1C>;

template <U32 BITS, std::size_t N>
constexpr auto reversePlacements(const std::array<U32, N>& placements) {
	std::array<U32, N> a;
	for (std::size_t i = 0; i < N; i++)
		a[i] = __builtin_bitreverse32(placements[i]) >> (32 - BITS);
	return a;
}

export template <U16 P0C, U16 P1C>
constexpr auto PAWNTABLE_P0_INV = reversePlacements<25>(PAWNTABLE_P0<P0C, P1C>);

export template <U16 P0C, U16 P1C>
constexpr auto PAWNTABLE_P1_INV = reversePlacements<25 - P0C>(PAWNTABLE_P1<P0C, P1C>);

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE U32 unrankFirstPieces(int ip) {
	if constexpr (!invert)
		return PAWNTABLE_P0<P0C, P1C>[ip];
	else
		return PAWNTABLE_P0_INV<P0C, P1C>[ip];
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE U32 unrankSecondPieces(int ip, U32 bbpOther) {
	if constexpr (!invert)
		return _pdep_u32(PAWNTABLE_P1<P0C, P1C>[ip], ~bbpOther);
	else
		return _pdep_u32(PAWNTABLE_P1_INV<P0C, P1C>[ip], ~bbpOther);
}

template <bool first, bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE U32 unrankKings(int ik, U32 bbp) {
	if constexpr (!invert)
		return _pdep_u32(1U << ik, bbp);
	else if constexpr (first)
		return _pdep_u32(1U << (P0C - 1) >> ik, bbp);
	else
		return _pdep_u32(1U << (P1C - 1) >> ik, bbp);
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE U32 unrankFirstKing(int ik, U32 bbp) {
	return unrankKings<true, invert, P0C, P1C>(ik, bbp);
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE U32 unrankSecondKing(int ik, U32 bbp) {
	return unrankKings<false, invert, P0C, P1C>(ik, bbp);
}

export template <bool invert, U16 P0C, U16 P1C, U16 N = P0C>
__FORCE_INLINE int rankFirstPieces(U32 bbp) {
	std::array<U32, N> ip;
	if constexpr (!invert) {
		for (int i = 0; i < static_cast<int>(ip.size()); i++) {
			ip[i] = std::countr_zero(bbp);
			bbp &= bbp - 1;
		}
	} else {
		for (int i = static_cast<int>(ip.size()); i-- > 0;) {
			ip[i] = 24 - std::countr_zero(bbp);
			bbp &= bbp - 1;
		}
	}

	int index = 0;
	for (int i = 0; i < static_cast<int>(ip.size()); i++) {
		U32 pawnIndex = 1;
		for (int j = 0; j <= i; j++)
			pawnIndex = (pawnIndex * ip[i]--) / (j + 1);
		index += pawnIndex;
	}
	return index;
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE int rankSecondPieces(U32 bbp, U32 bbpOther) {
	bbp = _pext_u32(bbp, ~bbpOther);
	if constexpr (invert)
		bbp <<= P0C;
	return rankFirstPieces<invert, P0C, P1C, P1C>(bbp);
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE int rankKings(U32 bbk, U32 bbp) {
	if constexpr (!invert)
		return std::popcount((bbk - 1) & bbp);
	else
		return std::popcount(-(bbk << 1) & bbp);
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE int rankFirstKing(U32 bbk, U32 bbp) {
	return rankKings<invert, P0C, P1C>(bbk, bbp);
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE int rankSecondKing(U32 bbk, U32 bbp) {
	return rankKings<invert, P0C, P1C>(bbk, bbp);
}
