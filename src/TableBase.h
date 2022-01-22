#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <atomic>

// the tablebase describes boards with player 0 to move.
// if player 1 is to move, use boardToIndex<true> which flips the board.
struct __attribute__((__packed__)) TableBase {
	typedef std::array<std::atomic<U64>*, PIECECOUNTMULT * KINGSMULT> Row;
	std::array<Row, CARDSMULT> tb;
	std::atomic<U64>* end;
	
    Row& operator [](int i) {
        return tb[i];
    }
    Row operator [](int i) const {
        return tb[i];
    }
};

TableBase* generateTB(const CardsInfo& cards);