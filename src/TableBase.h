#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <vector>
#include <atomic>


typedef std::vector<std::atomic<U64>> TableBaseRow;
typedef std::array<TableBaseRow, 30> TableBase;

typedef std::vector<U64> NATableBaseRow;
typedef std::array<NATableBaseRow, 30> NATableBase;

TableBase generateTB(const CardsInfo& cards);