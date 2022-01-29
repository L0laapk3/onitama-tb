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
#include <bitset>
#include <functional>


//#define NO_INLINE_INDEX

#if defined(NO_INLINE_INDEX) || !defined(NDEBUG)
	#define INLINE_INDEX_FN __attribute__((noinline))
#else
	#define INLINE_INDEX_FN __attribute__((always_inline)) inline 
#endif



void iterateTBCounts(const MoveBoard& reverseMoveBoard, std::function<void(U32, U32)> cb);


struct BoardIndex {
	U32 pieceCnt_kingsIndex;
	U32 pieceIndex;
};


constexpr U32 CARDSMULT = 30;
constexpr U32 KINGSMULT = 23*24+1;
constexpr U32 PIECECOUNTMULT = (TB_MEN / 2) * (TB_MEN / 2);


constexpr auto PIECES1MULT = [](){
	std::array<std::array<U32, TB_MEN/2>, TB_MEN/2> a{};
	for (int p0 = 0; p0 < TB_MEN/2; p0++)
		for (int p1 = 0; p1 < TB_MEN/2; p1++)
			a[p0][p1] = fact(23-p0, 23-p0-p1) / fact(p1);
	return a;
}();
#define PIECES0MULT PIECES1MULT[0]
constexpr auto PIECES10MULT = [](){
	std::array<std::array<U32, TB_MEN/2>, TB_MEN/2> a{};
	for (int p0 = 0; p0 < TB_MEN/2; p0++)
		for (int p1 = 0; p1 < TB_MEN/2; p1++)
			a[p0][p1] = PIECES0MULT[p0] * PIECES1MULT[p0][p1];
	return a;
}();
constexpr auto PIECES10KMULT = [](){
	std::array<std::array<U32, TB_MEN/2>, TB_MEN/2> a{};
	for (int p0 = 0; p0 < TB_MEN/2; p0++)
		for (int p1 = 0; p1 < TB_MEN/2; p1++)
			a[p0][p1] = KINGSMULT * PIECES0MULT[p0] * PIECES1MULT[p0][p1];
	return a;
}();

constexpr auto GENERATE_OFFSETS() {
	std::array<std::pair<U64, U64>, TB_MEN/2 * TB_MEN/2> a{};
	std::array<std::array<U32, TB_MEN/2>, TB_MEN/2> b{};
    int index = 0;
	for (int i = TB_MEN - 1; i-- > 0; )
        for (int j = i % 2; j <= TB_MEN; j += 2)
            for (int k = -1; k <= (j == 0 ? 0 : 1); k += 2)
                if (i - j >= 0 && i + j <= TB_MEN - 2) {
                    int p0c = (i - k * j) / 2, p1c = (i + k * j) / 2;
					b[p0c][p1c] = index * KINGSMULT;
                    a[index++] = { p0c, p1c };
                }
	// for (int p0c = TB_MEN/2-1; p0c --> 0; )
	// 	for (int p1c = TB_MEN/2-1; p1c --> 0; )
	// 		a[index++] = { p0c, p1c };
    return std::pair{ a, b };
};
constexpr auto OFFSET_ORDER = GENERATE_OFFSETS().first;
constexpr auto OFFSET_LOOKUP = GENERATE_OFFSETS().second;


template<bool includeSelf>
constexpr auto GENERATE_TABLE_SIZES() {
	std::array<std::array<U64, TB_MEN/2>, TB_MEN/2> a{};
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
	std::array<std::array<std::array<U32, 2>, TB_MEN/2>, TB_MEN/2> a{};
	for (int p0c = 0; p0c < TB_MEN/2; p0c++) {
		for (int p1c = 0; p1c < TB_MEN/2; p1c++) {
			U64 offset = 0;

			// when not all pieces are on the board, lzcnt/tzcnt returns 64. We include this here at compile-time in the offset tables.
			if (p0c < 4 && TB_MEN > 8) offset += PIECES1MULT[p0c][p1c] * 32 * 31 * 30 * 29 / 24;
			if (p0c < 3 && TB_MEN > 6) offset += PIECES1MULT[p0c][p1c] * 32 * 31 * 30 / 6;
			if (p0c < 2) offset += PIECES1MULT[p0c][p1c] * 32 * 31 / 2;
			if (p0c < 1) offset += PIECES1MULT[p0c][p1c] * 32;

			if (p1c < 4 && TB_MEN > 8) offset += 32 * 31 * 30 * 29 / 24;
			if (p1c < 3 && TB_MEN > 6) offset += 32 * 31 * 30 / 6;
			if (p1c < 2) offset += 32 * 31 / 2;
			if (p1c < 1) offset += 32;

			a[p0c][p1c][0] = offset;

			if (p0c == 4) offset += PIECES1MULT[p0c][p1c] * 32 * 31 * 30 * 29 / 24;
			if (p0c == 3) offset += PIECES1MULT[p0c][p1c] * 32 * 31 * 30 / 6;
			if (p0c == 2) offset += PIECES1MULT[p0c][p1c] * 32 * 31 / 2;
			if (p0c == 1) offset += PIECES1MULT[p0c][p1c] * 32;

			a[p0c][p1c][1] = offset;
		}
	}
	return a;
}();

constexpr auto TABLES_TWOKINGS = [](){
	std::array<std::array<U16, 32*25>, 2> a{ (U16)-1 };
	
	for (U64 inv = 0; inv < 2; inv++) {
		U16 i = 0;
		for (int j = 0; j < 25; j++)
			for (int k = 0; k < 25; k++) {
				if (k != j && j != PTEMPLE[0] && k != PTEMPLE[1])
					a[inv][(inv ? 24 - j : j)*32 + (inv ? 24 - k : k)] = i++;
			}
	}
    return a;
}();
constexpr auto TABLES_BBKINGS = [](){
	std::array<std::array<std::pair<U32, U32>, KINGSMULT>, 2> a{};
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
	std::array<U32, size> a{};
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
	std::array<U64, 30> a{};
	for (int i = 0; i < 30; i++) {
		U64 v = i + 3;
		a[i] = v * (v - 1) * (v - 2) * (v - 3) / 24;
	}
    return a;
}();
#endif
#if TB_MEN >= 8
constexpr auto MULTABLE3 = [](){
	std::array<U64, 31> a{};
	for (int i = 0; i < 31; i++) {
		U64 v = i + 2;
		a[i] = v * (v - 1) * (v - 2) / 6;
	}
    return a;
}();
#endif
constexpr auto MULTABLE2 = [](){
	std::array<U64, 32> a{};
	for (U64 i = 0; i < 32; i++) {
		U64 v = i + 1;
		a[i] = v * (v - 1) / 2;
	}
    return a;
}();






template <int maskMaxBits>
U64 INLINE_INDEX_FN boardToIndex_compactPawnBitboard(U64 bbp, U64 mask) {
#ifdef USE_PDEP
	return _pext_u64(bbp, ~mask);
#else
	U64 bbin = bbp;
	#pragma unroll
	for (int i = 0; i < maskMaxBits - 1; i++) {
		bbp += bbp & (mask ^ (mask - 1));
		mask &= mask - 1;
	}
	bbp += bbp & (mask - 1);
	bbp >>= maskMaxBits;
	assert(bbp == _pext_u64(bbin, ~mask));
	return bbp;
#endif
}

template <int maskMaxBits>
U64 INLINE_INDEX_FN indexToBoard_decompactPawnBitboard(U64 bbp, U64 mask) {
#ifdef USE_PDEP
	return _pdep_u64(bbp, ~mask);
#else
	U64 bbin = bbp;
	#pragma unroll
	for (int i = 0; i < maskMaxBits - 1; i++) {
		U64 bb = mask & -mask;
		bbp += bbp & ~(bb - 1);
		mask &= mask - 1;
	}
	bbp += bbp & ~(mask - 1);
	assert(bbp == _pdep_u64(bbin, ~mask));
	return bbp;
#endif
}



template <bool invert>
U32 INLINE_INDEX_FN boardToIndex_pawnBitboardToIndex(U64 bbpc, U64 shift) {

	// 0 means the piece has been taken and is not on the board
	// 1-x means the piece is on a square as given by bbp0c/bbp1c
	// we can achieve a reduction of 4!, we don't care about the permutation of the 4 pawns.
	// our algorithm to achieve this depends on p0 < p1 < p2 < p3, where 0 is treated as the largest number.
	U32 ip0, ip1, ip2, ip3;
	if (!invert) {
		ip0 = _tzcnt_u32(bbpc); // when not found, it will return 64 which is compensated by OFFSETS_SUB_EMPTY
		ip1 = _tzcnt_u32(bbpc &= bbpc-1);
		if (TB_MEN > 6) ip2 = _tzcnt_u32(bbpc &= bbpc-1);
		if (TB_MEN > 8) ip3 = _tzcnt_u32(bbpc &= bbpc-1);
	} else {
		bbpc <<= shift; // shift amount is the number of positions to shift. If there are 23 possible positions to scan, then it should be shifted by 32 - 23 = 9.
		ip0 = _lzcnt_u32(bbpc);
		ip1 = _lzcnt_u32(bbpc &= ~(1ULL << 31 >> ip0));
		if (TB_MEN > 6) ip2 = _lzcnt_u32(bbpc &= ~(1ULL << 31 >> ip1));
		if (TB_MEN > 8) ip3 = _lzcnt_u32(bbpc &= ~(1ULL << 31 >> ip2));
	}

	U32 r = 0;
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


struct BoardToIndexIntermediate {
	U32 mul;
	U32 rp1;
};
// boardToIndex<false>(board): given a board with player 0 to move, returns unique index for that board
// boardToIndex<true>(board): same but for player 1. Identical to boardToIndex<false>(board.invert())
template <bool invert>
BoardIndex INLINE_INDEX_FN boardToIndex(Board board, const MoveBoard& reverseMoveBoard, BoardToIndexIntermediate& im) {
	if (invert) {
		std::swap(board.bbp[0], board.bbp[1]);
		std::swap(board.bbk[0], board.bbk[1]);
	}
	
	U64 ik0 = _tzcnt_u64(board.bbk[0]); //attempt to replace table with logic: U64 ik0 = _tzcnt_u64(_pext_u64(board.bbk0, ~(1ULL << 2) & ~board.bbk1));
	U64 ik1 = _tzcnt_u64(board.bbk[1]);
	U32 rk = TABLES_TWOKINGS[invert][ik0*32 + ik1];

	board.bbp[1] &= ~board.bbk[1];
	U64 bbpc1 = boardToIndex_compactPawnBitboard<TB_MEN/2 + 1>(board.bbp[1], board.bbp[0] | board.bbk[1]); // P1 pawns skip over kings and P0 pawns

	board.bbp[0] &= ~board.bbk[0];
	U64 pp0cnt = _popcnt64(board.bbp[0]);
	U64 pp1cnt = _popcnt64(board.bbp[1]);
	U32 rpc = OFFSET_LOOKUP[pp0cnt][pp1cnt];
	im.mul = PIECES1MULT[pp0cnt][pp1cnt];
	auto& offsetArr = OFFSETS_SUB_EMPTY[pp0cnt][pp1cnt];

	// prevent king wins: any squares threatening the p1 king need to be masked out for p0.
	U64 p0CompactMask = board.bbk[0] | board.bbk[1] | reverseMoveBoard[ik1];
	// prevent temple wins: if p0 king is threatening temple win, one pawn needs to block the temple.
	bool templeWin = reverseMoveBoard[PTEMPLE[invert]] & board.bbk[0];
	if (templeWin) {
		assert(board.bbp[0] & (1 << PTEMPLE[invert]));
		board.bbp[0] &= ~(1 << PTEMPLE[invert]);
		p0CompactMask |= (1 << PTEMPLE[invert]);
	}
	
	U32 offset = offsetArr[templeWin];

	U64 bbpc0 = boardToIndex_compactPawnBitboard<11>(board.bbp[0], p0CompactMask); // P0 pawns skip over kings and opponent king threaten spaces

	im.rp1 = boardToIndex_pawnBitboardToIndex<invert>(bbpc1, 9 + pp0cnt); // possible positions: 23 - pp0cnt
	U32 rp0 = boardToIndex_pawnBitboardToIndex<invert>(bbpc0, 7 + _popcnt64(p0CompactMask)); // possible positions: 25 - popcnt(mask)

	return {
		.pieceCnt_kingsIndex = rpc + rk,
		.pieceIndex = rp0 * im.mul + im.rp1 - offset,
	};
}
template <bool invert>
BoardIndex INLINE_INDEX_FN boardToIndex(Board board, const MoveBoard& reverseMoveBoard) {
	BoardToIndexIntermediate im;
	return boardToIndex<invert>(board, reverseMoveBoard, im);
}


template <bool invert>
BoardIndex INLINE_INDEX_FN boardToIndexFromIntermediate(Board board, const MoveBoard& reverseMoveBoard, BoardIndex& bi, BoardToIndexIntermediate& im) {
	if (invert) {
		std::swap(board.bbp[0], board.bbp[1]);
		std::swap(board.bbk[0], board.bbk[1]);
	}

	board.bbp[0] &= ~board.bbk[0];
	U64 pp0cnt = _popcnt64(board.bbp[0]);
	U64 pp1cnt = _popcnt64(board.bbp[1]) - 1;
	auto& offsetArr = OFFSETS_SUB_EMPTY[pp0cnt][pp1cnt];

	U64 ik1 = _tzcnt_u64(board.bbk[1]);
	U64 p0CompactMask = board.bbk[0] | board.bbk[1] | reverseMoveBoard[ik1];
	
	bool templeWin = reverseMoveBoard[PTEMPLE[invert]] & board.bbk[0];
	if (templeWin) {
		assert(board.bbp[0] & (1 << PTEMPLE[invert]));
		board.bbp[0] &= ~(1 << PTEMPLE[invert]);
		p0CompactMask |= (1 << PTEMPLE[invert]);
	}
	
	U32 offset = offsetArr[templeWin];

	U64 bbpc0 = boardToIndex_compactPawnBitboard<11>(board.bbp[0], p0CompactMask); // P0 pawns skip over kings and opponent king threaten spaces
	U32 rp0 = boardToIndex_pawnBitboardToIndex<invert>(bbpc0, 7 + _popcnt64(p0CompactMask)); // possible positions: 25 - popcnt(mask)

	return {
		.pieceCnt_kingsIndex = bi.pieceCnt_kingsIndex,
		.pieceIndex = rp0 * im.mul + im.rp1 - offset,
	};
}





// indexToBoard<false>(index): given a unique index, returns the board with player 0 to move
// indexToBoard<true>(index): same but returns a board with player 1 to move. Identical to indexToBoard<false>(index).invert()
template<bool invert>
Board INLINE_INDEX_FN indexToBoard(BoardIndex bi, const MoveBoard& reverseMoveBoard) {

	U32 rk = bi.pieceCnt_kingsIndex % KINGSMULT;
	U32 rpc = bi.pieceCnt_kingsIndex / KINGSMULT;
	
	U64 bbk0, bbk1;
	std::tie(bbk0, bbk1) = TABLES_BBKINGS[invert][rk];
	U64 ik1 = _tzcnt_u64(bbk1);

	// p0 is not allowed to have any pieces on these squares:
	// - the squares occupied by the two kings
	// - the squares from which p1's king can be taken
	U64 p0CompactMask = bbk0 | bbk1 | reverseMoveBoard[ik1];

	// if p0 is threating a temple win, a pawn needs to block the temple. One pawn will be subtracted from p0,
	// and all other pawns will not be allowed to be on the temple square, so it is added to the mask.
	bool templeWin = reverseMoveBoard[PTEMPLE[invert]] & bbk0;
	if (templeWin) {
		p0CompactMask |= 1ULL << PTEMPLE[invert];
	}

	auto [p0c, p1c] = OFFSET_ORDER[rpc];
	U64 ip1 = bi.pieceIndex % PIECES1MULT[p0c][p1c];
	U64 ip0 = bi.pieceIndex / PIECES1MULT[p0c][p1c];

	U64 bbpc0 = PAWNTABLE_POINTERS[invert][p0c - templeWin][ip0];
	assert(_popcnt64(bbpc0) == p0c - templeWin);
	if (invert) {
		assert((bbpc0 & ((1ULL << (_popcnt64(p0CompactMask) - 2)) - 1)) == 0);
		bbpc0 >>= _popcnt64(p0CompactMask) - 2;
	}
	U64 bbpc1 = PAWNTABLE_POINTERS[invert][p1c][ip1];
	assert(_popcnt64(bbpc1) == p1c);
	if (invert) {
		assert((bbpc1 & ((1ULL << (p0c)) - 1)) == 0);
		bbpc1 >>= p0c;
	}

	U64 bbp0 = indexToBoard_decompactPawnBitboard<11>(bbpc0, p0CompactMask) | bbk0; // P0 pawns skip over kings

	if (templeWin) // add the temple pawn in to block the temple win
		bbp0 |= 1ULL << PTEMPLE[invert];

	U64 bbp1 = indexToBoard_decompactPawnBitboard<TB_MEN/2 + 1>(bbpc1, bbk1 | bbp0) | bbk1; // P1 pawns skip over kings and P0 pawns

	assert(bbp0 < (1 << 25));
	assert(bbp1 < (1 << 25));
	// assert(_popcnt64(bbp0) == p0c + 1);
	// assert(_popcnt64(bbp1) == p1c + 1);
	
	if (invert) {
		std::swap(bbp0, bbp1);
		std::swap(bbk0, bbk1);
	}

	return {
		.bbp = { bbp0, bbp1 },
		.bbk = { bbk0, bbk1 },
	};
}


void testIndexing(const CardsInfo& cards);
void exhaustiveIndexTest(const CardsInfo& cards);