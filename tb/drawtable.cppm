module;
#include <immintrin.h>
export module tb:drawtable;
import std;
import :types;
import :helper;
import :card;
import :index;
import :tablebase;

template <U16 TB_MEN>
constexpr U32 MAX_PC = TB_MEN / 2;

constexpr U32 binom(int n, int k) {
	return static_cast<U32>(fact(n, n - k) / fact(k));
}

// QUIET_OFFSET[q]: first quiet row of the quiet player having q pieces. A quiet row is (placement, king).
template <U16 TB_MEN>
constexpr auto QUIET_OFFSET = [] {
	std::array<U32, MAX_PC<TB_MEN> + 2> a{};
	for (U32 q = 1; q <= MAX_PC<TB_MEN>; q++)
		a[q + 1] = a[q] + binom(25, q) * q;
	return a;
}();

// VALUE_OFFSET[q][m]: first value of the moving player having m pieces, placed on the 25 - q squares free of quiet pieces.
template <U16 TB_MEN>
constexpr auto VALUE_OFFSET = [] {
	std::array<std::array<U32, MAX_PC<TB_MEN> + 2>, MAX_PC<TB_MEN> + 1> a{};
	for (U32 q = 1; q <= MAX_PC<TB_MEN>; q++)
		for (U32 m = 1; m <= MAX_PC<TB_MEN>; m++)
			a[q][m + 1] = a[q][m] + binom(25 - q, m);
	return a;
}();
static_assert(VALUE_OFFSET<10>[1][MAX_PC<10> + 1] <= 1 << 16, "moving placements must fit in 16 bits");

export template <U16 TB_MEN>
constexpr U32 NUM_DRAW_KEYS = QUIET_OFFSET<TB_MEN>[MAX_PC<TB_MEN> + 1] * 10 * MAX_PC<TB_MEN> * 3;

// Card perm bit 10 * side + hand, see CARDS_PERMUTATIONS.
static_assert([] {
	for (int p = 0; p < 30; p++)
		if (CARDS_PERMUTATIONS[p].playerCards[0] != CARDS_PERMUTATIONS[p % 10].playerCards[0])
			return false;
	return true;
}());

export template <U16 TB_MEN>
constexpr U32 drawKey(U32 quietRow, U32 quietHand, U32 movingKing, U32 sideCard) {
	return ((quietRow * 10 + quietHand) * MAX_PC<TB_MEN> + movingKing) * 3 + sideCard;
}


// Calls f(thread, quietRow, movingKing, value, unresolvedBits) for every entry with an unresolved card perm, concurrently.
// The stored side to move is the quiet player, the other side is the moving player.
template <U16 TB_MEN>
void forEachUnresolvedEntry(const typename TableBase<TB_MEN>::Storage& tb, auto&& f) {
	tb.forEachRow([&]<U16 QC, U16 MC> {
		const auto& row = tb.template getRow<QC, MC>();
		std::atomic<U32> nextIp0 = 0;
		std::vector<std::jthread> threads;
		for (unsigned t = 0; t < std::thread::hardware_concurrency(); t++)
			threads.emplace_back([&, t] {
				for (U32 ip0; (ip0 = nextIp0++) < row.size();) {
					const U32 bbq = unrankFirstPieces<false, QC, MC>(ip0);
					for (U32 ip1 = 0; ip1 < row[ip0].size(); ip1++) {
						const auto& block = row[ip0][ip1];
						int value = -1;
						for (int ikq = 0; ikq < QC; ikq++)
							for (int ikm = 0; ikm < MC; ikm++) {
								const U32 bits = block[ikq][ikm].load(std::memory_order_relaxed);
								if (!bits)
									continue;
								if (value < 0) {
									const U32 bbm = unrankSecondPieces<false, QC, MC>(ip1, bbq);
									value = static_cast<int>(VALUE_OFFSET<TB_MEN>[QC][MC] + rankSecondPieces<true, QC, MC>(bbm, bbq));
								}
								const U32 quietRow = QUIET_OFFSET<TB_MEN>[QC] + ip0 * QC + ikq;
								f(t, quietRow, static_cast<U32>(invertKingRank<MC>(ikm)), static_cast<U16>(value), bits);
							}
					}
				}
			});
	});
}

// Elias-Fano encoded set of the unresolved (drawn) positions.
export struct DrawTable {
	// table order from MSB to LSB:
	// -- IN THE INDEX TABLE (~2**26 single bit entries: a 0 per key, followed by a 1 per draw with that key)
	// pawn + king index of the quiet player (piece count implicitly included) (as viewed from the perspective of the quiet player)
	// cards index of the quiet player hand (0-9)
	// king index (0-4) of the moving player (as viewed from the perspective of the moving player)
	// total cards index (0-2)
	// -- IN THE VALUE TABLE (16 bits)
	// pawns permutation index of the moving player (piece count implicitly included) (as viewed from the perspective of the moving player)

	static constexpr U32 SAMPLE_KEYS = 64;

	U32 men = 0;
	U64 numKeys = 0;
	U64 numBits = 0;
	std::vector<U32> rowSamples; // rowSamples[i]: bit position of the 0 of key i * SAMPLE_KEYS
	std::vector<U64> highBits;	 // zero padded by at least one word, so a scan always ends on a 0
	std::vector<U16> lowBits;	 // values in key order, ascending within a key

	template <U16 TB_MEN>
	static DrawTable fromTableBase(const TableBase<TB_MEN>& table) {
		std::vector<std::vector<U64>> perThread(std::thread::hardware_concurrency());
		forEachUnresolvedEntry<TB_MEN>(*table.tb, [&](unsigned t, U32 quietRow, U32 movingKing, U16 value, U32 bits) {
			for (; bits; bits &= bits - 1) {
				const U32 perm = std::countr_zero(bits);
				perThread[t].push_back(U64{drawKey<TB_MEN>(quietRow, perm % 10, movingKing, perm / 10)} << 16 | value);
			}
		});
		std::vector<U64> draws;
		for (auto& v : perThread) {
			draws.insert(draws.end(), v.begin(), v.end());
			std::vector<U64>().swap(v);
		}
		std::ranges::sort(draws);

		DrawTable dt;
		dt.men = TB_MEN;
		dt.numKeys = NUM_DRAW_KEYS<TB_MEN>;
		dt.numBits = dt.numKeys + draws.size();
		dt.highBits.assign(dt.numBits / 64 + 2, 0);
		dt.lowBits.resize(draws.size());
		for (U64 i = 0; i < draws.size(); i++) {
			// The 0 of key k is preceded by the k earlier 0s and the 1s of every draw before it.
			const U64 pos = (draws[i] >> 16) + i + 1;
			dt.highBits[pos / 64] |= 1ULL << (pos % 64);
			dt.lowBits[i] = static_cast<U16>(draws[i]);
		}
		return dt;
	}

	bool contains(U32 key, U16 value) const {
		const U32 sample = rowSamples[key / SAMPLE_KEYS];
		U64 word = sample / 64;
		U64 zeros = ~highBits[word] & (~0ULL << (sample % 64));
		for (U32 skip = key % SAMPLE_KEYS;; zeros = ~highBits[++word]) {
			const U32 count = std::popcount(zeros);
			if (skip < count) {
				zeros = _pdep_u64(1ULL << skip, zeros);
				break;
			}
			skip -= count;
		}
		U64 pos = word * 64 + std::countr_zero(zeros);
		for (U64 i = pos - key;; i++) {
			pos++;
			if (!(highBits[pos / 64] >> (pos % 64) & 1))
				return false;
			if (lowBits[i] >= value)
				return lowBits[i] == value;
		}
	}

	// Checks every card perm of every entry that has a draw against the table, throws on a mismatch.
	template <U16 TB_MEN>
	void verify(const TableBase<TB_MEN>& table) const {
		if (men != TB_MEN)
			throw std::runtime_error("draw table men mismatch");
		std::atomic<U64> draws = 0, mismatches = 0;
		forEachUnresolvedEntry<TB_MEN>(*table.tb, [&](unsigned, U32 quietRow, U32 movingKing, U16 value, U32 bits) {
			U64 localMismatches = 0;
			for (U32 perm = 0; perm < 30; perm++)
				localMismatches += contains(drawKey<TB_MEN>(quietRow, perm % 10, movingKing, perm / 10), value) != static_cast<bool>(bits >> perm & 1);
			draws += std::popcount(bits);
			if (localMismatches)
				mismatches += localMismatches;
		});
		if (mismatches || draws != lowBits.size())
			throw std::runtime_error(std::format("draw table verification failed: {} mismatches, {} draws in table vs {} in draw table", mismatches.load(), draws.load(), lowBits.size()));
	}

	void toFile(std::ostream& os) const {
		const Header header{MAGIC, men, 0, numKeys, numBits, lowBits.size()};
		os.write(reinterpret_cast<const char*>(&header), sizeof(header));
		os.write(reinterpret_cast<const char*>(highBits.data()), highBits.size() * sizeof(U64));
		os.write(reinterpret_cast<const char*>(lowBits.data()), lowBits.size() * sizeof(U16));
		if (!os)
			throw std::runtime_error("failed to write draw table");
	}

	static DrawTable fromFile(std::istream& is) { // this is the only place that should fill in rowSamples;
		Header header;
		is.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!is || header.magic != MAGIC)
			throw std::runtime_error("not a draw table file");
		if (header.numBits != header.numKeys + header.numValues || header.numBits >= 1ULL << 32)
			throw std::runtime_error("corrupt draw table header");

		DrawTable dt;
		dt.men = header.men;
		dt.numKeys = header.numKeys;
		dt.numBits = header.numBits;
		dt.highBits.resize(dt.numBits / 64 + 2);
		dt.lowBits.resize(header.numValues);
		is.read(reinterpret_cast<char*>(dt.highBits.data()), dt.highBits.size() * sizeof(U64));
		is.read(reinterpret_cast<char*>(dt.lowBits.data()), dt.lowBits.size() * sizeof(U16));
		if (!is)
			throw std::runtime_error("truncated draw table file");

		U64 ones = 0;
		for (const U64 w : dt.highBits)
			ones += std::popcount(w);
		if (ones != header.numValues)
			throw std::runtime_error("corrupt draw table");

		dt.rowSamples.reserve(dt.numKeys / SAMPLE_KEYS + 1);
		U64 zerosBefore = 0;
		U64 nextKey = 0;
		for (U64 word = 0; nextKey < dt.numKeys; word++) {
			const U64 zeros = ~dt.highBits[word];
			const U32 count = std::popcount(zeros);
			for (; nextKey < dt.numKeys && nextKey < zerosBefore + count; nextKey += SAMPLE_KEYS)
				dt.rowSamples.push_back(static_cast<U32>(word * 64 + std::countr_zero(_pdep_u64(1ULL << (nextKey - zerosBefore), zeros))));
			zerosBefore += count;
		}
		return dt;
	}

private:
	static constexpr std::array<char, 8> MAGIC{'O', 'N', 'I', 'D', 'R', 'A', 'W', '1'};
	struct Header {
		std::array<char, 8> magic;
		U32 men;
		U32 reserved;
		U64 numKeys;
		U64 numBits;
		U64 numValues;
	};
};
