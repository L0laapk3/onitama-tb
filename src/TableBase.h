#pragma once

#include "mimalloc.h"

#include "Board.h"
#include "Index.hpp"
#include "Card.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <tuple>
#include <utility>




template <U16 TB_MEN>
struct TableBase {
	struct CardsEntry : std::atomic<U32> {
		CardsEntry() : std::atomic<U32>((1U << 30) - 1) {}
	};

	template <U16 ROW_I>
	using TableKingPermsP1 = std::array<CardsEntry, TB_MEN_ORDER<TB_MEN>[ROW_I].second + 1>;

	template <U16 ROW_I>
	using TableKingPermsP0 = std::array<TableKingPermsP1<ROW_I>, TB_MEN_ORDER<TB_MEN>[ROW_I].first + 1>;

	template <U16 ROW_I>
	using TableP1 = std::array<TableKingPermsP0<ROW_I>, PAWNTABLE_P1<TB_MEN, ROW_I>.size()>;

	template <U16 ROW_I>
	struct alignas(64) TableRow : std::array<TableP1<ROW_I>, PAWNTABLE_P0<TB_MEN, ROW_I>.size()> {
		TableRow() {}
	};

	template <std::size_t... I>
	static auto tableBaseStorage(std::index_sequence<I...>) -> std::tuple<TableRow<I>...>;

	using TableBaseStorage = decltype(tableBaseStorage(std::make_index_sequence<TB_MEN_ORDER<TB_MEN>.size()>{}));



	TableBase(const CardsInfo& cards);

	TableBaseStorage tb;
};
