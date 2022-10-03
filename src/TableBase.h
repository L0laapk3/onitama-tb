#pragma once

#include "mimalloc-new-delete.h"

#include "Board.h"
#include "Index.hpp"
#include "Card.hpp"

#include <array>
#include <vector>
#include <atomic>
#include <memory>
#include <functional>

// the tablebase describes boards with player 0 to move.
// if player 1 is to move, use boardToIndex<true> which flips the board


template <U16 TB_MEN, bool STORE_WIN>
struct TableBase;

typedef std::vector<std::atomic<U64>, mi_stl_allocator<std::atomic<U64>>> MemVec;
typedef std::vector<unsigned char, mi_stl_allocator<unsigned char>> CompMemRowVec;
typedef std::vector<CompMemRowVec, mi_stl_allocator<CompMemRowVec>> MemCompVec;

template <U16 TB_MEN, bool STORE_WIN>
class RefRowWrapper {
public:
	typedef std::array<U32, PIECECOUNTMULT<TB_MEN> * KINGSMULT + 1> RefRow;
	RefRow refs;

	MemVec mem;
	MemCompVec memComp;

	U8 usesSinceModified = 0;
	bool isCompressed = false;
	bool isDecompressed = false;
	bool isChanged = true;

    std::atomic<U64>* operator [](int i) {
		return &mem.data()[refs[i]];
	}

	void initiateCompress(TableBase<TB_MEN, STORE_WIN>& tb);
	void partialCompress(int section);
	void finishCompress(TableBase<TB_MEN, STORE_WIN>& tb);
	
	void initiateDecompress(TableBase<TB_MEN, STORE_WIN>& tb);
	void partialDecompress(int section);
	void finishDecompress(TableBase<TB_MEN, STORE_WIN>& tb, bool keepCompressedMem);
};

template <U16 TB_MEN, bool STORE_WIN>
struct TableBase {
	typedef std::array<RefRowWrapper<TB_MEN, STORE_WIN>, CARDSMULT> RefTable;
	RefTable refTable;
	
    RefRowWrapper<TB_MEN, STORE_WIN>& operator [](int i) {
		return refTable[i];
	}
	
	void determineUnloads(U8 cardI, std::function<void(RefRowWrapper<TB_MEN, STORE_WIN>& row)> cb);
	template<U8 numRows>
	void determineUnloads(U8 nextLoadCardI, std::array<U8, numRows> rowsI, std::function<void(RefRowWrapper<TB_MEN, STORE_WIN>& row)> cb);

	U64 cnt_0;
	U64 cnt;

	std::atomic<long long> memory_remaining;

	std::vector<uint64_t> storeSparse(const CardsInfo& cards);
	
	static std::unique_ptr<TableBase<TB_MEN, STORE_WIN>> generate(const CardsInfo& cards, U64 memory_allowance);
};





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

template <bool STORE_WIN, typename T>
constexpr auto getOfInterestBits(T& bits) {
	return STORE_WIN ? bits >> 32 : ~bits;
}
template <bool STORE_WIN, typename T>
constexpr auto countOfInterestBits(T& bits) {
	return STORE_WIN ? _popcnt32(bits >> 32) : _popcnt64(~bits);
}




template <bool STORE_WIN>
constexpr typename std::conditional<STORE_WIN, U32, U64>::type getResolvedBits(U64 entry) {
	return entry;
}


#include "TableBase.hpp"
#include "TableBaseCompress.hpp"
#include "TableBaseStore.hpp"