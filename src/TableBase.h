#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <atomic>

// the tablebase describes boards with player 0 to move.

struct TableBaseRow {
	std::atomic<U64>* begin;
	U64 size;
};
typedef std::array<std::array<TableBaseRow, PIECECOUNTMULT * KINGSMULT>, CARDSMULT> TableBase;

TableBase* generateTB(const CardsInfo& cards);