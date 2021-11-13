#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <valarray>
#include <atomic>

// the tablebase describes boards with player 0 to move.

typedef std::valarray<std::atomic<U64>> TableBaseRow;
typedef std::array<TableBaseRow, CARDSMULT * PIECECOUNTMULT * KINGSMULT> TableBase;

TableBase generateTB(const CardsInfo& cards);