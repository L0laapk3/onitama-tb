#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <vector>
#include <atomic>


typedef std::vector<std::atomic<U64>> TableBaseRow;
typedef std::array<TableBaseRow, 30> TableBase;

TableBase generateTB(const CardsInfo& cards);