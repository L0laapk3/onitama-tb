#include "Board.h"

#include "Helper.h"

#include <array>
#include <utility>

#define TB_MEN 10

constexpr U64 KINGSMULT = 24 + 23*23;

constexpr auto PIECES0MULT = [](){
	std::array<U64, TB_MEN/2> a;
	for (int i = 0; i < TB_MEN/2; i++)
		a[i] = fact(23, 23-i) / fact(i);
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
constexpr auto GENERATE_TABLE_SIZES() {
	std::array<std::array<U64, TB_MEN/2>, TB_MEN/2> a;
    U64 offset_cumul = 0;
	for (auto& pc : OFFSET_ORDER) {
        a[pc.first][pc.second] = offset_cumul;
        offset_cumul += KINGSMULT * (fact(23, 23-pc.first) / fact(pc.first)) * (fact(23-pc.first, 23-pc.first-pc.second) / fact(pc.second));
    }
	return std::pair{ a, offset_cumul };
}
constexpr auto OFFSETS = GENERATE_TABLE_SIZES().first;
constexpr U64 TB_SIZE = GENERATE_TABLE_SIZES().second;

constexpr auto TABLE_TWOKINGS = [](){
	std::array<U32, 25*25> a;
	U32 i = 0;
	for (int j = 0; j < 25; j++)
		for (int k = 0; k < 25; k++)
            a[j*25 + k] = k != j && j != 2 && k != 22 ? i++ : -1;
    return a;
}();
constexpr auto TABLE_BBKINGS = [](){
	std::array<std::pair<U32, U32>, KINGSMULT> a;
	U32 i = 0;
	for (int j = 0; j < 25; j++)
		for (int k = 0; k < 25; k++)
			if (k != j && j != 2 && k != 22)
				a[i++] = { 1ULL << j, 1ULL << k };
	return a;
}();

template<int pawns>
constexpr auto GENERATE_PAWN_TABLE() {
	const U64 size = fact(23, 23-pawns) / fact(pawns);
	std::array<U32, size> a{};
    return a;
};

constexpr auto TABLE_ONEPAWN = GENERATE_PAWN_TABLE<1>();
constexpr auto TABLE_TWOPAWNS = GENERATE_PAWN_TABLE<2>();
#if TB_MEN >= 8
	constexpr auto TABLE_THREEPAWNS = GENERATE_PAWN_TABLE<3>();
#endif
#if TB_MEN >= 10
	constexpr auto TABLE_FOURPAWNS = GENERATE_PAWN_TABLE<4>();
#endif


template <bool invert>
U64 Board::toIndex(const Board& board) {

	U64 ik0 = _tzcnt_u64(board.bbk0); //attempt to replace table with logic: U64 ik0 = _tzcnt_u64(_pext_u64(board.bbk0, ~(1ULL << 2) & ~board.bbk1));
	U64 ik1 = _tzcnt_u64(board.bbk1);
	U64 rk = TABLE_TWOKINGS[ik0*25 + ik1];

	U64 bbpp0 = board.bbp0 - board.bbk0;
	U64 bbpp1 = board.bbp1 - board.bbk1;
	
	U64 bbpc0 = _pext_u64(bbpp0, ~board.bbk0 & ~board.bbp1); // P0 pawns skip over P0 king and P1 pawns
	U64 bbpc1 = _pext_u64(bbpp0, ~board.bbk0 & ~board.bbk1); // P1 pawns skip over kings
	
	U64 pp0cnt = _popcnt64(bbpp0);
	U64 pp1cnt = _popcnt64(bbpp1);
	U64 offset = OFFSETS[pp0cnt][pp1cnt];

	// 0 means the piece has been taken and is not on the board
	// 1-x means the piece is on a square as given by bbp0c/bbp1c
	// we can achieve a reduction of 4!, we don't care about the permutation of the 4 pawns.
	// our algorithm to achieve this depends on p0 < p1 < p2 < p3, where 0 is treated as the largest number.
	bbpc0 <<= !invert ? 1 : 38;
	bbpc1 <<= !invert ? 1 : 38;
	
	U64 ip0p0, ip0p1, ip0p2, ip0p3, ip1p0, ip1p1, ip1p2, ip1p3;
	if (!invert) {
		ip0p0 = _tzcnt_u64(bbpc0) & 31; // when not found, it will return 64 which is cut off by the & operation
		ip0p1 = _tzcnt_u64(bbpc0 &= bbpc0-1) & 31;
		ip0p2 = _tzcnt_u64(bbpc0 &= bbpc0-1) & 31;
		ip0p3 = _tzcnt_u64(bbpc0 &= bbpc0-1) & 31;

		ip1p0 = _tzcnt_u64(bbpc1) & 31;
		ip1p1 = _tzcnt_u64(bbpc1 &= bbpc1-1) & 31;
		ip1p2 = _tzcnt_u64(bbpc1 &= bbpc1-1) & 31;
		ip1p3 = _tzcnt_u64(bbpc1 &= bbpc1-1) & 31;
	} else {
		ip0p0 = _lzcnt_u64(bbpc0) & 31;
		ip0p1 = _lzcnt_u64(bbpc0 -= bbpc0 & -bbpc0) & 31;
		ip0p2 = _lzcnt_u64(bbpc0 -= bbpc0 & -bbpc0) & 31;
		ip0p3 = _lzcnt_u64(bbpc0 -= bbpc0 & -bbpc0) & 31;
		
		ip1p0 = _lzcnt_u64(bbpc1) & 31;
		ip1p1 = _lzcnt_u64(bbpc1 -= bbpc1 & -bbpc1) & 31;
		ip1p2 = _lzcnt_u64(bbpc1 -= bbpc1 & -bbpc1) & 31;
		ip1p3 = _lzcnt_u64(bbpc1 -= bbpc1 & -bbpc1) & 31;
	}

	U64 rp1 = ip1p3 * (ip1p3-1) * (ip1p3-2) * (ip1p3-3);
	rp1    += ip1p2 * (ip1p2-1) * (ip1p2-2) * 4;
	rp1    += ip1p1 * (ip1p1-1) * 12;
	U64 r = rp1 / 24 + ip1p0;

	r *= PIECES0MULT[pp0cnt];
	U64 rp0 = ip0p3 * (ip0p3-1) * (ip0p3-2) * (ip0p3-3);
	rp0    += ip0p2 * (ip0p2-1) * (ip0p2-2) * 4;
	rp0    += ip0p1 * (ip0p1-1) * 12;
	r += rp0 / 24 + ip0p0;

	r *= KINGSMULT;
	r += rk;
	return offset + r;
}



constexpr std::array<const U32*, 4> PAWNTABLE_POINTERS = { &TABLE_ONEPAWN[0], &TABLE_TWOPAWNS[0], &TABLE_THREEPAWNS[0], &TABLE_FOURPAWNS[0] };
struct FromIndexHalfReturn {
	U64 ik;
	U64 bbpc0;
	U64 bbpc1;
};
template <bool invert, int p0c, int p1c>
FromIndexHalfReturn fromIndexHelper(U64 index) {
	constexpr p0mult = PIECES0MULT[p0c];
	U64 ik = index % (KINGSMULT * p0mult);
	index /= KINGSMULT * p0mult;
	U64 ip0 = index % p0mult;
	index /= ip0;
	return {
		.ik = ik,
		.bbpc0 = *(PAWNTABLE_POINTERS[p0c] + ip0),
		.bbpc1 = *(PAWNTABLE_POINTERS[p1c] + ip1),
	};
}

template <bool invert>
Board Board::fromIndex(U64 index) {

	FromIndexHalfReturn bbStuff;
	if      (index < OFFSETS[4][3]) bbStuff = fromIndexHelper<invert, 4, 4>(index                );
	else if (index < OFFSETS[3][4]) bbStuff = fromIndexHelper<invert, 4, 3>(index - OFFSETS[4][3]);
	else if (index < OFFSETS[3][3]) bbStuff = fromIndexHelper<invert, 3, 4>(index - OFFSETS[3][4]);
	else if (index < OFFSETS[4][2]) bbStuff = fromIndexHelper<invert, 3, 3>(index - OFFSETS[3][3]);
	else if (index < OFFSETS[2][4]) bbStuff = fromIndexHelper<invert, 4, 2>(index - OFFSETS[4][2]);
	else if (index < OFFSETS[3][2]) bbStuff = fromIndexHelper<invert, 2, 4>(index - OFFSETS[2][4]);
	else if (index < OFFSETS[2][3]) bbStuff = fromIndexHelper<invert, 3, 2>(index - OFFSETS[3][2]);
	else if (index < OFFSETS[4][1]) bbStuff = fromIndexHelper<invert, 2, 3>(index - OFFSETS[2][3]);
	else if (index < OFFSETS[1][4]) bbStuff = fromIndexHelper<invert, 4, 1>(index - OFFSETS[4][1]);
	else if (index < OFFSETS[2][2]) bbStuff = fromIndexHelper<invert, 1, 4>(index - OFFSETS[1][4]);
	else if (index < OFFSETS[3][1]) bbStuff = fromIndexHelper<invert, 2, 2>(index - OFFSETS[2][2]);
	else if (index < OFFSETS[1][3]) bbStuff = fromIndexHelper<invert, 3, 1>(index - OFFSETS[3][1]);
	else if (index < OFFSETS[4][0]) bbStuff = fromIndexHelper<invert, 1, 3>(index - OFFSETS[1][3]);
	else if (index < OFFSETS[0][4]) bbStuff = fromIndexHelper<invert, 4, 0>(index - OFFSETS[4][0]);
	else if (index < OFFSETS[2][1]) bbStuff = fromIndexHelper<invert, 0, 4>(index - OFFSETS[0][4]);
	else if (index < OFFSETS[1][2]) bbStuff = fromIndexHelper<invert, 2, 1>(index - OFFSETS[2][1]);
	else if (index < OFFSETS[3][0]) bbStuff = fromIndexHelper<invert, 1, 2>(index - OFFSETS[1][2]);
	else if (index < OFFSETS[0][3]) bbStuff = fromIndexHelper<invert, 3, 0>(index - OFFSETS[3][0]);
	else if (index < OFFSETS[1][1]) bbStuff = fromIndexHelper<invert, 0, 3>(index - OFFSETS[0][3]);
	else if (index < OFFSETS[2][0]) bbStuff = fromIndexHelper<invert, 1, 1>(index - OFFSETS[1][1]);
	else if (index < OFFSETS[0][2]) bbStuff = fromIndexHelper<invert, 2, 0>(index - OFFSETS[2][0]);
	else if (index < OFFSETS[1][0]) bbStuff = fromIndexHelper<invert, 0, 2>(index - OFFSETS[0][2]);
	else if (index < OFFSETS[0][1]) bbStuff = fromIndexHelper<invert, 1, 0>(index - OFFSETS[1][0]);
	else if (index < OFFSETS[0][0]) bbStuff = fromIndexHelper<invert, 0, 1>(index - OFFSETS[0][1]);
	else                            bbStuff = fromIndexHelper<invert, 0, 0>(index - OFFSETS[0][0]);

	U64 bbk0, bbk1;
	std::tie(bbk0, bbk1) = TABLE_BBKINGS[bbStuff.ik];

	bbp1 = _pdep_u64(bbp1, ~bbk0 & ~bbk1) | bbp1; // P1 pawns skip over kings
	bbp0 = _pdep_u64(bbp0, ~bbk0 & ~bbp1) | bbk0; // P0 pawns skip over P0 king and P1 pawns

	return {
		.bbp0 = bbp0,
		.bbp1 = bbp1,
		.bbk0 = bbk0,
		.bbk1 = bbk1,
	};
}



