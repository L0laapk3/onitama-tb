export module tb:tablebase_test;
import std;
import :types;
import :card;
import :board;
import :index;
import :tablebase;

// Brute force reference, deliberately independent of the move/card tables used by the generator.
// A position is seen from the player to move: own pieces move "up" (+5 per row) towards temple 22,
// the opponent's temple is 2. The table entry of a position is decoded without inversion.
namespace tablebase_test_impl {

using Moves = std::array<std::array<U32, 25>, 5>;

Moves forwardMoves(const CardSet& cards) {
	Moves moves{};
	for (int c = 0; c < 5; c++)
		for (int b = 0; b < 25; b++) {
			if (!((cards[c] >> b) & 1))
				continue;
			const int dr = b / 5 - 2, dc = b % 5 - 2;
			for (int s = 0; s < 25; s++) {
				const int r = s / 5 + dr, col = s % 5 + dc;
				if (r >= 0 && r < 5 && col >= 0 && col < 5)
					moves[c][s] |= 1U << (5 * r + col);
			}
		}
	return moves;
}

// [perm][slot]: permutation (opponent to move) after the mover played playerCards[0][slot].
constexpr auto NEXT_PERM = [] {
	const auto sameHand = [](std::array<U8, 2> a, std::array<U8, 2> b) {
		return (a[0] == b[0] && a[1] == b[1]) || (a[0] == b[1] && a[1] == b[0]);
	};
	std::array<std::array<U8, 2>, 30> result{};
	for (U8 p = 0; p < 30; p++) {
		const auto& perm = CARDS_PERMUTATIONS[p];
		for (int slot = 0; slot < 2; slot++) {
			auto moverHand = perm.playerCards[0];
			moverHand[slot] = perm.sideCard;
			const U8 used = perm.playerCards[0][slot];
			U8 found = 255;
			for (U8 q = 0; q < 30; q++) {
				const auto& next = CARDS_PERMUTATIONS[q];
				if (sameHand(next.playerCards[0], perm.playerCards[1]) && sameHand(next.playerCards[1], moverHand) && next.sideCard == used)
					found = q;
			}
			result[p][slot] = found;
		}
	}
	return result;
}();

U32 rotate(U32 bb) {
	return __builtin_bitreverse32(bb) >> 7;
}

struct Pos {
	U32 p, o, kp, ko;
	U8 perm;
};

bool winIn0(const Pos& s) {
	return !s.kp || s.kp == 1U << 22 || s.ko == 1U << 2;
}

bool winIn1(const Moves& moves, const Pos& s) {
	for (U8 card : CARDS_PERMUTATIONS[s.perm].playerCards[0]) {
		if (moves[card][std::countr_zero(s.kp)] & ~s.p & (1U << 22))
			return true;
		for (U32 pieces = s.p; pieces; pieces &= pieces - 1)
			if (moves[card][std::countr_zero(pieces)] & s.ko)
				return true;
	}
	return false;
}

// Calls f(child) for every legal move (child is seen from the opponent), stops and returns true once f does.
bool anyChild(const Moves& moves, const Pos& s, auto&& f) {
	for (int slot = 0; slot < 2; slot++) {
		const U8 card = CARDS_PERMUTATIONS[s.perm].playerCards[0][slot];
		for (U32 pieces = s.p; pieces; pieces &= pieces - 1) {
			const U32 src = pieces & -pieces;
			for (U32 targets = moves[card][std::countr_zero(pieces)] & ~s.p; targets; targets &= targets - 1) {
				const U32 dst = targets & -targets;
				const U32 p = (s.p & ~src) | dst;
				const U32 kp = s.kp == src ? dst : s.kp;
				const Pos child{ rotate(s.o & ~dst), rotate(p), rotate(s.ko & ~dst), rotate(kp), NEXT_PERM[s.perm][slot] };
				if (f(child))
					return true;
			}
		}
	}
	return false;
}

bool winOrLossIn1(const Moves& moves, const Pos& s) {
	return winIn0(s) || winIn1(moves, s);
}

// Every move hands the opponent a win in 0 or 1.
bool lossIn2(const Moves& moves, const Pos& s) {
	return !winOrLossIn1(moves, s) && !anyChild(moves, s, [&](const Pos& c) { return !winOrLossIn1(moves, c); });
}

bool winIn3(const Moves& moves, const Pos& s) {
	return !winOrLossIn1(moves, s) && anyChild(moves, s, [&](const Pos& c) { return lossIn2(moves, c); });
}

enum Category { WIN0, WIN1, LOSS2, WIN3, OTHER, CATEGORIES };
constexpr std::array<const char*, CATEGORIES> CATEGORY_NAMES{ "win in 0", "win in 1", "loss in 2", "win in 3", "other" };

struct Results {
	std::array<std::atomic<U64>, CATEGORIES> count{};
	// [category]: positions the brute force puts in this category but whose table bit disagrees.
	std::array<std::atomic<U64>, CATEGORIES> mismatches{};
	std::mutex printMutex;
	int printed = 0;

	void report(const Pos& s, Category cat, bool resolved) {
		std::scoped_lock lock(printMutex);
		if (printed++ >= 10)
			return;
		const auto& perm = CARDS_PERMUTATIONS[s.perm];
		std::println("MISMATCH: brute force says {}, table says {} (perm {}: mover {}{}, opponent {}{}, side {})", CATEGORY_NAMES[cat],
			resolved ? "resolved" : "unresolved", s.perm, perm.playerCards[0][0], perm.playerCards[0][1], perm.playerCards[1][0],
			perm.playerCards[1][1], perm.sideCard);
		Board{ { s.p, s.o }, { s.kp, s.ko } }.print();
	}
};

template <U16 TB_MEN, U32 ROW>
void checkRow(const Moves& moves, const auto& tb, Results& results) {
	const auto& row = std::get<ROW>(tb);
	std::atomic<U32> next = 0;
	const auto work = [&] {
		for (U32 ip0; (ip0 = next++) < row.size();) {
			const U32 p = unrankFirstPieces<TB_MEN, ROW, false>(ip0);
			for (U32 ip1 = 0; ip1 < row[ip0].size(); ip1++) {
				const U32 o = unrankSecondPieces<TB_MEN, ROW, false>(ip1, p);
				for (U32 ik0 = 0; ik0 < row[ip0][ip1].size(); ik0++) {
					const U32 kp = unrankFirstKing<TB_MEN, ROW, false>(ik0, p);
					for (U32 ik1 = 0; ik1 < row[ip0][ip1][ik0].size(); ik1++) {
						const U32 ko = unrankSecondKing<TB_MEN, ROW, false>(ik1, o);
						const U32 entry = row[ip0][ip1][ik0][ik1].load(std::memory_order_relaxed);
						for (U8 perm = 0; perm < 30; perm++) {
							const Pos s{ p, o, kp, ko, perm };
							const bool resolved = !((entry >> perm) & 1);
							Category cat;
							bool ok;
							if (winIn0(s))
								cat = WIN0, ok = resolved;
							else if (winIn1(moves, s))
								cat = WIN1, ok = resolved;
							else if (lossIn2(moves, s))
								cat = LOSS2, ok = resolved;
							else if (!resolved)
								cat = OTHER, ok = true; // proving "not a win in 3" for all of these is too slow
							else if (winIn3(moves, s))
								cat = WIN3, ok = true;
							else
								cat = OTHER, ok = false;
							results.count[cat]++;
							if (!ok) {
								results.mismatches[cat]++;
								results.report(s, cat, resolved);
							}
						}
					}
				}
			}
		}
	};
	std::vector<std::jthread> threads;
	for (unsigned i = 0; i < std::max(1U, std::thread::hardware_concurrency()); i++)
		threads.emplace_back(work);
}

} // namespace tablebase_test_impl

// Builds only step 1 (depth 2) and checks every entry against a brute force search.
// Every win in 0/1 and loss in 2 must be resolved, and every other resolved bit must be a win in 3
// (found by the reverse movegen of step 1).
export template <U16 TB_MEN>
bool testStepOne(const CardsInfo& cards) {
	using namespace tablebase_test_impl;
	TableBase<TB_MEN> tb(cards, 2);
	const Moves moves = forwardMoves(cards.cards);
	Results results;
	[&]<U32... ROW>(std::integer_sequence<U32, ROW...>) {
		(checkRow<TB_MEN, ROW>(moves, *tb.tb, results), ...);
	}(std::make_integer_sequence<U32, std::tuple_size_v<typename TableBase<TB_MEN>::TableBaseStorage>>{});

	U64 totalMismatches = 0;
	for (int cat = 0; cat < CATEGORIES; cat++) {
		std::println("{:>10}: {:>12} states, {:>10} mismatches", CATEGORY_NAMES[cat], results.count[cat].load(), results.mismatches[cat].load());
		totalMismatches += results.mismatches[cat];
	}
	std::println("{}", totalMismatches ? "STEP 1 TEST FAILED" : "STEP 1 TEST PASSED");
	return !totalMismatches;
}
