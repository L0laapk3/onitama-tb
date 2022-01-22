#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <atomic>
#include <memory>

// the tablebase describes boards with player 0 to move.
// if player 1 is to move, use boardToIndex<true> which flips the board.
struct TableBase {
	typedef std::array<std::atomic<U64>*, PIECECOUNTMULT * KINGSMULT> Row;
	
	#pragma pack(push, 1)
	std::array<Row, CARDSMULT> tb;
	std::atomic<U64>* end;
	#pragma pack(pop)

	std::vector<std::atomic<U64>> mem;
	
    Row& operator [](int i) {
        return tb[i];
    }
    Row operator [](int i) const {
        return tb[i];
    }
};

std::unique_ptr<TableBase> generateTB(const CardsInfo& cards);