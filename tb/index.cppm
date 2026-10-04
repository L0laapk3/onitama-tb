module;
#include <immintrin.h>
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
	int index = 0;
	for (int i = TB_MEN - 1; i-- > 0;)
		for (int j = i % 2; j <= TB_MEN; j += 2)
			for (int k = -1; k <= (j == 0 ? 0 : 1); k += 2)
				if (i - j >= 0 && i + j <= TB_MEN - 2) {
					int p0c = (i - k * j) / 2, p1c = (i + k * j) / 2;
					a[index++] = {static_cast<U16>(p0c + 1), static_cast<U16>(p1c + 1)};
				}
	return a;
}();

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

export template <U16 TB_MEN, std::size_t ROW>
constexpr auto& PAWNTABLE_P0 = PIECE_PLACEMENTS<25, PIECE_COUNTS<TB_MEN>[ROW].p0c>;

export template <U16 TB_MEN, std::size_t ROW>
constexpr auto& PAWNTABLE_P1 = PIECE_PLACEMENTS<25 - PIECE_COUNTS<TB_MEN>[ROW].p0c, PIECE_COUNTS<TB_MEN>[ROW].p1c>;

template <U32 BITS, std::size_t N>
constexpr auto reversePlacements(const std::array<U32, N>& placements) {
	std::array<U32, N> a;
	for (std::size_t i = 0; i < N; i++)
		a[i] = __builtin_bitreverse32(placements[i]) >> (32 - BITS);
	return a;
}

export template <U16 TB_MEN, std::size_t ROW>
constexpr auto PAWNTABLE_P0_INV = reversePlacements<25>(PAWNTABLE_P0<TB_MEN, ROW>);

export template <U16 TB_MEN, std::size_t ROW>
constexpr auto PAWNTABLE_P1_INV = reversePlacements<25 - PIECE_COUNTS<TB_MEN>[ROW].p0c>(PAWNTABLE_P1<TB_MEN, ROW>);

export template <U16 TB_MEN, std::size_t ROW, bool invert>
U32 unrankFirstPieces(int ip) {
	if constexpr (!invert)
		return PAWNTABLE_P0<TB_MEN, ROW>[ip];
	else
		return PAWNTABLE_P0_INV<TB_MEN, ROW>[ip];
}

export template <U16 TB_MEN, std::size_t ROW, bool invert>
U32 unrankSecondPieces(int ip, U32 bbpOther) {
	if constexpr (!invert)
		return _pdep_u32(PAWNTABLE_P1<TB_MEN, ROW>[ip], ~bbpOther);
	else
		return _pdep_u32(PAWNTABLE_P1_INV<TB_MEN, ROW>[ip], ~bbpOther);
}

template <U16 TB_MEN, std::size_t ROW, bool first, bool invert>
U32 unrankKings(int ik, U32 bbp) {
	if constexpr (!invert)
		return _pdep_u32(1U << ik, bbp);
	else if constexpr (first)
		return _pdep_u32(1U << (PIECE_COUNTS<TB_MEN>[ROW].p0c - 1) >> ik, bbp);
	else
		return _pdep_u32(1U << (PIECE_COUNTS<TB_MEN>[ROW].p1c - 1) >> ik, bbp);
}

export template <U16 TB_MEN, std::size_t ROW, bool invert>
U32 unrankFirstKing(int ik, U32 bbp) {
	return unrankKings<TB_MEN, ROW, true, invert>(ik, bbp);
}

export template <U16 TB_MEN, std::size_t ROW, bool invert>
U32 unrankSecondKing(int ik, U32 bbp) {
	return unrankKings<TB_MEN, ROW, false, invert>(ik, bbp);
}

export template <U16 TB_MEN, std::size_t ROW, bool invert>
int rankFirstPieces(U32 bbp) {
	std::array<U32, invert ? PIECE_COUNTS<TB_MEN>[ROW].p1c : PIECE_COUNTS<TB_MEN>[ROW].p0c> ip;
	if constexpr (!invert) {
		for (int i = 0; i < static_cast<int>(ip.size()); i++) {
			ip[i] = std::countr_zero(bbp);
			bbp &= bbp - 1;
		}
	} else {
		for (int i = static_cast<int>(ip.size()); i-- > 0;) {
			ip[i] = 31 - std::countr_zero(bbp);
			bbp &= bbp - 1;
		}
	}

	int index = 0;
	for (int i = 0; i < static_cast<int>(ip.size()); i++) {
		U32 pawnIndex = 1;
		for (int j = 0; j < i; j++)
			pawnIndex *= ip[i]-- / (j + 1);
		index += pawnIndex;
	}
	return index;
}

export template <U16 TB_MEN, std::size_t ROW, bool invert>
int rankSecondPieces(U32 bbp, U32 bbpOther) {
	bbp = _pdep_u32(bbp, ~bbpOther);
	return rankFirstPieces<TB_MEN, ROW, invert>(bbp);
}

export template <U16 TB_MEN, std::size_t ROW, bool invert>
int rankKings(U32 bbk, U32 bbp) {
	if constexpr (!invert)
		return std::popcount((bbk - 1) & bbp);
	else
		return std::popcount(((1U << 31) - bbk) & bbp);
}

export template <U16 TB_MEN, std::size_t ROW, bool invert>
int rankFirstKing(U32 bbk, U32 bbp) {
	return rankKings<TB_MEN, ROW, invert>(bbk, bbp);
}

export template <U16 TB_MEN, std::size_t ROW, bool invert>
int rankSecondKing(U32 bbk, U32 bbp) {
	return rankKings<TB_MEN, ROW, invert>(bbk, bbp);
}
