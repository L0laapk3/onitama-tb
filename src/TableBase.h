#pragma once

#include "Board.h"
#include "Index.h"
#include "Card.hpp"

#include <vector>
#include <atomic>
#include <chrono>


typedef std::vector<std::atomic<U64>> TableBaseRow;
typedef std::array<TableBaseRow, 30> TableBase;

typedef std::vector<U64> NATableBaseRow;
typedef std::array<NATableBaseRow, 30> NATableBase;


struct TBGen {
	std::unique_ptr<TableBase> tb;
	U64 cnt;
	std::chrono::duration<long long, std::nano> time;
};

template <bool print>
TBGen generateTB(const CardsInfo& cards);

void benchTB(U64 runs);