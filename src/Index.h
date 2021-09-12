#pragma once

#include "Config.h"

#include "Types.h"
#include "Helper.h"
#include "Board.h"

#include <array>
#include <tuple>
#include <utility>
#include <cassert>
#include "x86intrin.h"
#include <xmmintrin.h>
#include <iostream>
#include <bitset>
#include <algorithm>


// #define NO_INLINE_INDEX

#ifdef NO_INLINE_INDEX
	#define INLINE_INDEX_FN __attribute__((noinline))
#else
	#define INLINE_INDEX_FN __attribute__((always_inline)) inline 
#endif



constexpr U64 KINGSMULT = 24 + 23*23;

constexpr auto PIECES1MULT = [](){
	std::array<std::array<U64, TB_MEN/2>, TB_MEN/2> a;
	for (int p0 = 0; p0 < TB_MEN/2; p0++)
		for (int p1 = 0; p1 < TB_MEN/2; p1++)
			a[p0][p1] = fact(23-p0, 23-p0-p1) / fact(p1);
	return a;
}();
#define PIECES0MULT PIECES1MULT[0]
constexpr auto PIECES10MULT = [](){
	std::array<std::array<U64, TB_MEN/2>, TB_MEN/2> a;
	for (int p0 = 0; p0 < TB_MEN/2; p0++)
		for (int p1 = 0; p1 < TB_MEN/2; p1++)
			a[p0][p1] = PIECES0MULT[p0] * PIECES1MULT[p0][p1];
	return a;
}();
constexpr auto PIECES10KMULT = [](){
	std::array<std::array<U64, TB_MEN/2>, TB_MEN/2> a;
	for (int p0 = 0; p0 < TB_MEN/2; p0++)
		for (int p1 = 0; p1 < TB_MEN/2; p1++)
			a[p0][p1] = KINGSMULT * PIECES0MULT[p0] * PIECES1MULT[p0][p1];
	return a;
}();

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
        offset_cumul += KINGSMULT * PIECES10MULT[pc.first][pc.second];
		if (includeSelf) a[pc.first][pc.second] = offset_cumul;
    }
	return std::pair{ a, offset_cumul };
}
constexpr U64 TB_ROW_SIZE = GENERATE_TABLE_SIZES<false>().second;

constexpr auto OFFSETS = GENERATE_TABLE_SIZES<false>().first;
constexpr auto MAX_INDEX = GENERATE_TABLE_SIZES<true>().first;



constexpr auto OFFSETS_SUB_EMPTY = [](){
	std::array<std::array<U64, TB_MEN/2>, TB_MEN/2> a;
	for (int p0c = 0; p0c < TB_MEN/2; p0c++) {
		for (int p1c = 0; p1c < TB_MEN/2; p1c++) {
			U64 offset = OFFSETS[p0c][p1c];

			// when not all pieces are on the board, lzcnt/tzcnt returns 64. We include this here at compile-time in the offset tables.
			if (p0c < 4 && TB_MEN > 8) offset -= 32 * 31 * 30 * 29 / 24;
			if (p0c < 3 && TB_MEN > 6) offset -= 32 * 31 * 30 / 6;
			if (p0c < 2) offset -= 32 * 31 / 2;
			if (p0c < 1) offset -= 32;

			if (p1c < 4 && TB_MEN > 8) offset -= PIECES0MULT[p0c] * 32 * 31 * 30 * 29 / 24;
			if (p1c < 3 && TB_MEN > 6) offset -= PIECES0MULT[p0c] * 32 * 31 * 30 / 6;
			if (p1c < 2) offset -= PIECES0MULT[p0c] * 32 * 31 / 2;
			if (p1c < 1) offset -= PIECES0MULT[p0c] * 32;

			a[p0c][p1c] = offset;
		}
	}
	return a;
}();

constexpr auto TABLE_TWOKINGS = [](){
	std::array<U16, 32*25> a{ (U16)-1 };
	U16 i = 0;
	for (int j = 0; j < 25; j++)
		for (int k = 0; k < 25; k++)
			if (k != j && j != PTEMPLE[0] && k != PTEMPLE[1])
          		a[j*32 + k] = i++;
    return a;
}();
constexpr auto TABLES_BBKINGS = [](){
	std::array<std::array<std::pair<U32, U32>, KINGSMULT>, 2> a;
	for (U64 inv = 0; inv < 2; inv++) {
		U32 i = 0;
		for (int j = 0; j < 25; j++)
			for (int k = 0; k < 25; k++)
				if (k != j && j != PTEMPLE[0] && k != PTEMPLE[1])
					a[inv][i++] = { 1ULL << (inv ? 24 - j : j), 1ULL << (inv ? 24 - k : k) };
	}
	return a;
}();



template<int pawns, int size, bool invert>
constexpr void GENERATE_PAWN_TABLE_PAWN(U32 bb, int remaining, std::array<U32, size>& a, std::array<U32, 4> p, U32 index) {
	if (remaining <= 0) {
		U64 rp = p[3] * (p[3]-1) * (p[3]-2) * (p[3]-3);
		rp    += p[2] * (p[2]-1) * (p[2]-2) * 4;
		rp    += p[1] * (p[1]-1) * 12;
		if (invert) {
			U32 bbrev = 0;
			for (U64 i = 0; i < 23; i++) {
				bbrev = (bbrev << 1) + (bb & 1);
				bb >>= 1;
			}
			bb = bbrev;
		}
		a[rp / 24 + p[0]] = bb;
	} else
		for (p[index] = 0; p[index] < 23; p[index]++) {
			U64 bbp = 1 << p[index];
			if (bbp > bb)
				GENERATE_PAWN_TABLE_PAWN<pawns, size, invert>(bbp | bb, remaining - 1, a, p, index + 1);
		}
}
template<int pawns, bool invert>
constexpr auto GENERATE_PAWN_TABLE() {
	const U64 size = fact(23, 23-pawns) / fact(pawns);
	std::array<U32, size> a;
	GENERATE_PAWN_TABLE_PAWN<pawns, size, invert>(0, pawns, a, { 0 }, 0);
    return a;
};

constexpr auto TABLE_ZEROPAWNS = GENERATE_PAWN_TABLE<0, false>();
constexpr auto TABLE_ONEPAWN = GENERATE_PAWN_TABLE<1, false>();
constexpr auto TABLE_TWOPAWNS = GENERATE_PAWN_TABLE<2, false>();
#if TB_MEN >= 8
	constexpr auto TABLE_THREEPAWNS = GENERATE_PAWN_TABLE<3, false>();
#endif
#if TB_MEN >= 10
	constexpr auto TABLE_FOURPAWNS = GENERATE_PAWN_TABLE<4, false>();
#endif
constexpr auto TABLE_ZEROPAWNS_INV = GENERATE_PAWN_TABLE<0, true>();
constexpr auto TABLE_ONEPAWN_INV = GENERATE_PAWN_TABLE<1, true>();
constexpr auto TABLE_TWOPAWNS_INV = GENERATE_PAWN_TABLE<2, true>();
#if TB_MEN >= 8
	constexpr auto TABLE_THREEPAWNS_INV = GENERATE_PAWN_TABLE<3, true>();
#endif
#if TB_MEN >= 10
	constexpr auto TABLE_FOURPAWNS_INV = GENERATE_PAWN_TABLE<4, true>();
#endif
#if TB_MEN == 6
	constexpr std::array<std::array<const U32*, 3>, 2> PAWNTABLE_POINTERS = {{
		{ &TABLE_ZEROPAWNS[0], &TABLE_ONEPAWN[0], &TABLE_TWOPAWNS[0] },
		{ &TABLE_ZEROPAWNS_INV[0], &TABLE_ONEPAWN_INV[0], &TABLE_TWOPAWNS_INV[0] },
	}};
#elif TB_MEN == 8
	constexpr std::array<std::array<const U32*, 4>, 2> PAWNTABLE_POINTERS = {{
		{ &TABLE_ZEROPAWNS[0], &TABLE_ONEPAWN[0], &TABLE_TWOPAWNS[0], &TABLE_THREEPAWNS[0] },
		{ &TABLE_ZEROPAWNS_INV[0], &TABLE_ONEPAWN_INV[0], &TABLE_TWOPAWNS_INV[0], &TABLE_THREEPAWNS_INV[0] },
	}};
#else
	constexpr std::array<std::array<const U32*, 5>, 2> PAWNTABLE_POINTERS = {{
		{ &TABLE_ZEROPAWNS[0], &TABLE_ONEPAWN[0], &TABLE_TWOPAWNS[0], &TABLE_THREEPAWNS[0], &TABLE_FOURPAWNS[0] },
		{ &TABLE_ZEROPAWNS_INV[0], &TABLE_ONEPAWN_INV[0], &TABLE_TWOPAWNS_INV[0], &TABLE_THREEPAWNS_INV[0], &TABLE_FOURPAWNS_INV[0] },
	}};
#endif


#if TB_MEN >= 10
constexpr auto MULTABLE4 = [](){
	std::array<U64, 30> a;
	for (int i = 0; i < 30; i++) {
		U64 v = i + 3;
		a[i] = v * (v - 1) * (v - 2) * (v - 3) / 24;
	}
    return a;
}();
#endif
#if TB_MEN >= 8
constexpr auto MULTABLE3 = [](){
	std::array<U64, 31> a;
	for (int i = 0; i < 31; i++) {
		U64 v = i + 2;
		a[i] = v * (v - 1) * (v - 2) / 6;
	}
    return a;
}();
#endif
constexpr auto MULTABLE2 = [](){
	std::array<U64, 32> a;
	for (U64 i = 0; i < 32; i++) {
		U64 v = i + 1;
		a[i] = v * (v - 1) / 2;
	}
    return a;
}();



template <bool invert>
U64 INLINE_INDEX_FN boardToIndex_kings(Board board) {
	if (invert)
		std::swap(board.bbk[0], board.bbk[1]);

	U64 ik0 = invert ? _lzcnt_u64(board.bbk[0]) - 39 : _tzcnt_u64(board.bbk[0]); //attempt to replace table with logic: U64 ik0 = _tzcnt_u64(_pext_u64(board.bbk0, ~(1ULL << 2) & ~board.bbk1));
	U64 ik1 = invert ? _lzcnt_u64(board.bbk[1]) - 39 : _tzcnt_u64(board.bbk[1]);
	return TABLE_TWOKINGS[ik0*32 + ik1];
}




template <bool invert, bool player>
U64 INLINE_INDEX_FN boardToIndex_pawns_A(Board board) {
	if (invert) {
		std::swap(board.bbp[0], board.bbp[1]);
		std::swap(board.bbk[0], board.bbk[1]);
	}

	U64 bbpp = board.bbp[player] - board.bbk[player];
	U64 bbpc = _pext_u64(bbpp, (player ? ~board.bbp[0] : ~board.bbk[0]) & ~board.bbk[1]); // P0 pawns skip over kings, P1 pawns skip over kings and P0 pawns

	return bbpc;
}

template <bool invert>
U64 INLINE_INDEX_FN boardToIndex_pawns_B(U64 bbpc, U64 shiftOffset) {

	// 0 means the piece has been taken and is not on the board
	// 1-x means the piece is on a square as given by bbp0c/bbp1c
	// we can achieve a reduction of 4!, we don't care about the permutation of the 4 pawns.
	// our algorithm to achieve this depends on p0 < p1 < p2 < p3, where 0 is treated as the largest number.
	U64 ip0, ip1, ip2, ip3;
	if (!invert) {
		ip0 = _tzcnt_u32(bbpc); // when not found, it will return 64 which is compensated by OFFSETS_SUB_EMPTY
		ip1 = _tzcnt_u32(bbpc &= bbpc-1);
		if (TB_MEN > 6) ip2 = _tzcnt_u32(bbpc &= bbpc-1);
		if (TB_MEN > 8) ip3 = _tzcnt_u32(bbpc &= bbpc-1);
	} else {
		bbpc <<= 9 + shiftOffset;
		ip0 = _lzcnt_u32(bbpc);
		ip1 = _lzcnt_u32(bbpc &= ~(1ULL << 31 >> ip0));
		if (TB_MEN > 6) ip2 = _lzcnt_u32(bbpc &= ~(1ULL << 31 >> ip1));
		if (TB_MEN > 8) ip3 = _lzcnt_u32(bbpc &= ~(1ULL << 31 >> ip2));
	}

	U64 r = 0;
	#if TB_MEN >= 10
		r += MULTABLE4[ip3 - 3];
	#endif
	#if TB_MEN >= 8
		r += MULTABLE3[ip2 - 2];
	#endif
	r += MULTABLE2[ip1 - 1];
	r += ip0;
	return r;
}


struct PartialIndex {
	U64 rk;
	U64 rp0;
	U64 rp1;
	U64 pp0cnt;
	U64 pp1cnt;
};
template <bool invert, bool doK, bool doP0, bool doP1>
U64 INLINE_INDEX_FN boardToIndexPartialSet(Board board, PartialIndex& pi) {
	
	U64 ik;
	if (doK) pi.rk = boardToIndex_kings<invert>(board);
	assert(pi.rk == boardToIndex_kings<invert>(board));
	U64 bbpc0, bbpc1;
	if (doP0 || true) bbpc0 = boardToIndex_pawns_A<invert, false>(board);
	if (doP1 || true) bbpc1 = boardToIndex_pawns_A<invert, true>(board);
	
	if (doP0) pi.pp0cnt = _popcnt64(bbpc0);
	assert(pi.pp0cnt == _popcnt64(bbpc0));
	if (doP1) pi.pp1cnt = _popcnt64(bbpc1);
	assert(pi.pp1cnt == _popcnt64(bbpc1));

	if (doP0) pi.rp0 = boardToIndex_pawns_B<invert>(bbpc0, 0);
	assert(pi.rp0 == boardToIndex_pawns_B<invert>(bbpc0, 0));
	if (doP1) pi.rp1 = boardToIndex_pawns_B<invert>(bbpc1, pi.pp0cnt);
	assert(pi.rp1 == boardToIndex_pawns_B<invert>(bbpc1, pi.pp0cnt));
	
	U64 offset = OFFSETS_SUB_EMPTY[pi.pp0cnt][pi.pp1cnt];

	U64 r = 0;

	r += pi.rk;

	r *= PIECES1MULT[pi.pp0cnt][pi.pp1cnt];
	r += pi.rp1;

	r *= PIECES0MULT[pi.pp0cnt];
	r += pi.rp0;

	r += offset;
	assert(r < TB_ROW_SIZE);
	return r;
}

template <bool invert, bool doK, bool doP0, bool doP1>
U64 INLINE_INDEX_FN boardToIndexPartial(Board board, PartialIndex pi) {
	return boardToIndexPartialSet<invert, doK, doP0, doP1>(board, pi);
}

template <bool invert>
U64 INLINE_INDEX_FN boardToIndex(Board board) {
	PartialIndex pi;
	return boardToIndexPartial<invert, true, true, true>(board, pi);
}



struct FromIndexHalfReturn {
	U64 ik;
	U64 bbpc0;
	U64 bbpc1;
};
template <bool invert, bool piInvert, int p0c, int p1c>
FromIndexHalfReturn INLINE_INDEX_FN fromIndexHelper(U64 index, PartialIndex& pi) {
	index -= OFFSETS[p0c][p1c];
	constexpr U64 p0mult = PIECES0MULT[p0c];
	constexpr U64 p1mult = PIECES1MULT[p0c][p1c];

	U64 ip0 = index % p0mult;
	index /= p0mult;
	U64 ip1 = index % p1mult;
	U64 ik = index / p1mult;

	// if (piInvert) {
	// 	pi.rp0 = ip0;
	// 	pi.rp1 = ip1;
	// 	pi.rk = ik;
	// } else {
	// 	// TODO
	// }

	// pi.pp0cnt = piInvert ? p1c : p0c;
	// pi.pp1cnt = piInvert ? p0c : p1c;

	// if (ik == 0)
	// 	std::cout << p0c << " " << p1c << " " << ik << " " << ip0 << " " << ip1 << std::endl;
	// assert(ik != 0 || ip0 != 210);

	return {
		.ik = ik,
		.bbpc0 = *(PAWNTABLE_POINTERS[invert][p0c] + ip0),
		.bbpc1 = *(PAWNTABLE_POINTERS[invert][p1c] + ip1) >> (invert ? p0c : 0),
	};
}

template<bool invert, bool piInvert>
Board INLINE_INDEX_FN indexToBoard(U64 index, PartialIndex& pi) {

	FromIndexHalfReturn bbStuff;
	if (0);
#if TB_MEN >= 8
#if TB_MEN >= 10
	else if (index < MAX_INDEX[4][4]) bbStuff = fromIndexHelper<invert, piInvert, 4, 4>(index, pi);
	else if (index < MAX_INDEX[4][3]) bbStuff = fromIndexHelper<invert, piInvert, 4, 3>(index, pi);
	else if (index < MAX_INDEX[3][4]) bbStuff = fromIndexHelper<invert, piInvert, 3, 4>(index, pi);
#endif
	else if (index < MAX_INDEX[3][3]) bbStuff = fromIndexHelper<invert, piInvert, 3, 3>(index, pi);
#if TB_MEN >= 10
	else if (index < MAX_INDEX[4][2]) bbStuff = fromIndexHelper<invert, piInvert, 4, 2>(index, pi);
	else if (index < MAX_INDEX[2][4]) bbStuff = fromIndexHelper<invert, piInvert, 2, 4>(index, pi);
#endif
	else if (index < MAX_INDEX[3][2]) bbStuff = fromIndexHelper<invert, piInvert, 3, 2>(index, pi);
	else if (index < MAX_INDEX[2][3]) bbStuff = fromIndexHelper<invert, piInvert, 2, 3>(index, pi);
#if TB_MEN >= 10
	else if (index < MAX_INDEX[4][1]) bbStuff = fromIndexHelper<invert, piInvert, 4, 1>(index, pi);
	else if (index < MAX_INDEX[1][4]) bbStuff = fromIndexHelper<invert, piInvert, 1, 4>(index, pi);
#endif
#endif
	else if (index < MAX_INDEX[2][2]) bbStuff = fromIndexHelper<invert, piInvert, 2, 2>(index, pi);
#if TB_MEN >= 8
	else if (index < MAX_INDEX[3][1]) bbStuff = fromIndexHelper<invert, piInvert, 3, 1>(index, pi);
	else if (index < MAX_INDEX[1][3]) bbStuff = fromIndexHelper<invert, piInvert, 1, 3>(index, pi);
#if TB_MEN >= 10
	else if (index < MAX_INDEX[4][0]) bbStuff = fromIndexHelper<invert, piInvert, 4, 0>(index, pi);
	else if (index < MAX_INDEX[0][4]) bbStuff = fromIndexHelper<invert, piInvert, 0, 4>(index, pi);
#endif
#endif
	else if (index < MAX_INDEX[2][1]) bbStuff = fromIndexHelper<invert, piInvert, 2, 1>(index, pi);
	else if (index < MAX_INDEX[1][2]) bbStuff = fromIndexHelper<invert, piInvert, 1, 2>(index, pi);
#if TB_MEN >= 8
	else if (index < MAX_INDEX[3][0]) bbStuff = fromIndexHelper<invert, piInvert, 3, 0>(index, pi);
	else if (index < MAX_INDEX[0][3]) bbStuff = fromIndexHelper<invert, piInvert, 0, 3>(index, pi);
#endif
	else if (index < MAX_INDEX[1][1]) bbStuff = fromIndexHelper<invert, piInvert, 1, 1>(index, pi);
	else if (index < MAX_INDEX[2][0]) bbStuff = fromIndexHelper<invert, piInvert, 2, 0>(index, pi);
	else if (index < MAX_INDEX[0][2]) bbStuff = fromIndexHelper<invert, piInvert, 0, 2>(index, pi);
	else if (index < MAX_INDEX[1][0]) bbStuff = fromIndexHelper<invert, piInvert, 1, 0>(index, pi);
	else if (index < MAX_INDEX[0][1]) bbStuff = fromIndexHelper<invert, piInvert, 0, 1>(index, pi);
	else                              bbStuff = fromIndexHelper<invert, piInvert, 0, 0>(index, pi);

	U64 bbk0, bbk1;
	std::tie(bbk0, bbk1) = TABLES_BBKINGS[invert][bbStuff.ik];

	U64 bbp0 = _pdep_u64(bbStuff.bbpc0, ~bbk0 & ~bbk1) | bbk0; // P0 pawns skip over kings
	U64 bbp1 = _pdep_u64(bbStuff.bbpc1, ~bbk1 & ~bbp0) | bbk1; // P1 pawns skip over kings and P0 pawns
	
	if (invert) {
		std::swap(bbp0, bbp1);
		std::swap(bbk0, bbk1);
	}

	assert(bbp0 < (1 << 25));
	assert(bbp1 < (1 << 25));

	return {
		.bbp = { bbp0, bbp1 },
		.bbk = { bbk0, bbk1 },
	};
}

template<bool invert>
Board INLINE_INDEX_FN indexToBoard(U64 index) {
	PartialIndex pi;
	return indexToBoard<invert, false>(index, pi);
}


void testIndexing();