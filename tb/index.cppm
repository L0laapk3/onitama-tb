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
constexpr auto PIECE_COUNT_ORDER = [] {
	constexpr int MAX_PC = TB_MEN / 2;
	std::array<PieceCount, MAX_PC * (MAX_PC + 1) / 2> a;
	std::size_t index = 0;
	for (int sum = 2; sum <= 2 * MAX_PC; sum++)
		for (int p0c = std::max(1, sum - MAX_PC); p0c <= sum / 2; p0c++)
			a[index++] = {static_cast<U16>(p0c), static_cast<U16>(sum - p0c)};
	return a;
}();

// The stored rows: both orientations of every piece count.
export template <U16 TB_MEN>
constexpr auto ROW_ORDER = [] {
	std::array<PieceCount, TB_MEN / 2 * TB_MEN / 2> a;
	std::size_t index = 0;
	for (const auto [p0c, p1c] : PIECE_COUNT_ORDER<TB_MEN>) {
		a[index++] = {p0c, p1c};
		if (p0c != p1c)
			a[index++] = {p1c, p0c};
	}
	return a;
}();

// Row with the given piece counts, -1 if it is not part of the table.
export template <U16 TB_MEN>
constexpr int rowIndex(int p0c, int p1c) {
	for (int i = 0; i < static_cast<int>(ROW_ORDER<TB_MEN>.size()); i++)
		if (ROW_ORDER<TB_MEN>[i].p0c == p0c && ROW_ORDER<TB_MEN>[i].p1c == p1c)
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

// The same king index as seen from the other player, for a side with PC pieces.
export template <U16 PC>
__FORCE_INLINE int invertKingRank(int ik) {
	return PC - 1 - ik;
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE int rankFirstKing(U32 bbk, U32 bbp) {
	return rankKings<invert, P0C, P1C>(bbk, bbp);
}

export template <bool invert, U16 P0C, U16 P1C>
__FORCE_INLINE int rankSecondKing(U32 bbk, U32 bbp) {
	return rankKings<invert, P0C, P1C>(bbk, bbp);
}
