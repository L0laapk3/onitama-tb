#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <atomic>

// the tablebase describes boards with player 0 to move.
// if player 1 is to move, use boardToIndex<true> which flips the board.
typedef std::array<std::array<std::atomic<U64>*, PIECECOUNTMULT * KINGSMULT + 1>, CARDSMULT> TableBase;

TableBase* generateTB(const CardsInfo& cards);