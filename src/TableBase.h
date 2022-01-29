#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <atomic>
#include <memory>
#include <ostream>

// the tablebase describes boards with player 0 to move.
// if player 1 is to move, use boardToIndex<true> which flips the board.
struct TableBase {
	typedef std::array<std::atomic<U64>*, PIECECOUNTMULT * KINGSMULT + 1> RefRow;
	std::array<RefRow, CARDSMULT> refTable;

	std::vector<std::atomic<U64>> mem;
	
    RefRow& operator [](int i) {
        return refTable[i];
    }
    RefRow operator [](int i) const {
        return refTable[i];
    }

	void dump(std::basic_ostream<char>& os);
};

std::unique_ptr<TableBase> generateTB(const CardsInfo& cards);