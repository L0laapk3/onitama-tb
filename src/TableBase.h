#pragma once

#include "Board.h"
#include "Index.hpp"
#include "Card.hpp"

#include <array>
#include <vector>
#include <atomic>
#include <memory>

// the tablebase describes boards with player 0 to move.
// if player 1 is to move, use boardToIndex<true> which flips the board


template <U16 TB_MEN, bool STORE_WIN>
struct TableBase;

template <U16 TB_MEN, bool STORE_WIN>
class RefRowWrapper {
public:
	typedef std::array<U32, PIECECOUNTMULT<TB_MEN> * KINGSMULT + 1> RefRow;
	RefRow refs;
	std::vector<std::atomic<U64>> mem;
	std::vector<unsigned char> memComp;

	std::atomic<bool> isCompressed = true;
	std::atomic<bool> isBusy = false;

    std::atomic<U64>* operator [](int i) {
		assert(!isCompressed);
		return &mem.data()[refs[i]];
	}

	void compress(TableBase<TB_MEN, STORE_WIN>& tb);
	void decompress(TableBase<TB_MEN, STORE_WIN>& tb, U16 cardI);
	void allocateDecompressed(U64 size, TableBase<TB_MEN, STORE_WIN>& tb, U16 cardI);
};
extern U64 totalDecompressions;
extern U64 totalLoads;

template <U16 TB_MEN, bool STORE_WIN>
struct TableBase {
	typedef std::array<RefRowWrapper<TB_MEN, STORE_WIN>, CARDSMULT> RefTable;
	RefTable refTable;
	
    RefRowWrapper<TB_MEN, STORE_WIN>& operator [](int i) {
		return refTable[i];
	}

	U64 cnt_0;
	U64 cnt;

	std::atomic<long long> memory_remaining;

	// std::vector<unsigned char> compress();
	// static std::vector<U64> decompressToIndices(const std::vector<unsigned char>& compressed);
	// void testCompression();
};


template <U16 TB_MEN, bool STORE_WIN>
std::unique_ptr<TableBase<TB_MEN, STORE_WIN>> generateTB(const CardsInfo& cards);



template <bool STORE_WIN>
constexpr U64 NUM_BOARDS_PER_U64 = STORE_WIN ? 32 : 64;

template <bool STORE_WIN, typename T>
constexpr auto countResolved(T& bits) {
	return STORE_WIN ? _popcnt32(bits) : _popcnt64(bits);
}
template <bool STORE_WIN>
constexpr auto WIN_SHIFT_BITS = [](){
	std::array<U64, NUM_BOARDS_PER_U64<STORE_WIN>> a{};
	for (U64 i = 0; i < NUM_BOARDS_PER_U64<STORE_WIN>; i++)
		a[i] = (STORE_WIN ? 0x100000001ULL : 0x1ULL) << i;
	return a;
}();
template <bool STORE_WIN, typename T>
constexpr U64 getWinBits(T pos) {
	return STORE_WIN ? WIN_SHIFT_BITS<STORE_WIN>[pos] : 0x1ULL << pos;
}

template <bool STORE_WIN>
constexpr typename std::conditional<STORE_WIN, U32, U64>::type getResolvedBits(U64 entry) {
	return entry;
}


#include "TableBase.hpp"
#include "TableBaseCompress.hpp"