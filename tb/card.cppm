export module tb:card;
import std;
import :types;

export constexpr U32 BOAR = 0b00000'00100'01010'00000'00000;
export constexpr U32 COBRA = 0b00000'01000'00010'01000'00000;
export constexpr U32 CRAB = 0b00000'00100'10001'00000'00000;
export constexpr U32 CRANE = 0b00000'00100'00000'01010'00000;
export constexpr U32 DRAGON = 0b00000'10001'00000'01010'00000;
export constexpr U32 EEL = 0b00000'00010'01000'00010'00000;
export constexpr U32 ELEPHANT = 0b00000'01010'01010'00000'00000;
export constexpr U32 FROG = 0b00000'00010'00001'01000'00000;
export constexpr U32 GOOSE = 0b00000'00010'01010'01000'00000;
export constexpr U32 HORSE = 0b00000'00100'00010'00100'00000;
export constexpr U32 MANTIS = 0b00000'01010'00000'00100'00000;
export constexpr U32 MONKEY = 0b00000'01010'00000'01010'00000;
export constexpr U32 OX = 0b00000'00100'01000'00100'00000;
export constexpr U32 RABBIT = 0b00000'01000'10000'00010'00000;
export constexpr U32 ROOSTER = 0b00000'01000'01010'00010'00000;
export constexpr U32 TIGER = 0b00100'00000'00000'00100'00000;

export constexpr std::array<U32, 16> ALL_CARDS{
	BOAR, COBRA, CRAB, CRANE, DRAGON, EEL, ELEPHANT, FROG, GOOSE, HORSE, MANTIS, MONKEY, OX, RABBIT, ROOSTER, TIGER
};

export struct CardPermutation {
	std::array<std::array<U8, 2>, 2> playerCards;
	U8 sideCard;
};

export constexpr std::array<CardPermutation, 30> CARDS_PERMUTATIONS = {{
	{ 2, 3, 0, 1, 4 },
	{ 1, 2, 0, 4, 3 },
	{ 0, 3, 1, 2, 4 },
	{ 2, 4, 0, 1, 3 },
	{ 0, 2, 1, 3, 4 },
	{ 1, 4, 0, 2, 3 },
	{ 0, 3, 1, 4, 2 },
	{ 1, 2, 0, 3, 4 },
	{ 0, 4, 1, 2, 3 },
	{ 1, 4, 0, 3, 2 },
	{ 0, 1, 2, 3, 4 },
	{ 0, 2, 1, 4, 3 },
	{ 1, 3, 0, 2, 4 },
	{ 0, 1, 2, 4, 3 },
	{ 0, 1, 3, 4, 2 },
	{ 0, 2, 3, 4, 1 },
	{ 1, 2, 3, 4, 0 },
	{ 2, 4, 0, 3, 1 },
	{ 1, 3, 2, 4, 0 },
	{ 0, 4, 1, 3, 2 },
	{ 2, 3, 0, 4, 1 },
	{ 1, 4, 2, 3, 0 },
	{ 2, 3, 1, 4, 0 },
	{ 0, 4, 2, 3, 1 },
	{ 1, 3, 0, 4, 2 },
	{ 3, 4, 0, 2, 1 },
	{ 2, 4, 1, 3, 0 },
	{ 3, 4, 0, 1, 2 },
	{ 0, 3, 2, 4, 1 },
	{ 3, 4, 1, 2, 0 },
}};

export constexpr std::array<std::array<std::array<U8, 2>, 2>, 30> CARDS_SWAP = {{
	{ 27, 3,  22, 20 },
	{ 20, 24, 16, 7  },
	{ 29, 8,  28, 6  },
	{ 27, 0,  26, 17 },
	{ 26, 19, 15, 11 },
	{ 25, 12, 21, 9  },
	{ 22, 11, 28, 2  },
	{ 17, 9,  16, 1  },
	{ 29, 2,  23, 19 },
	{ 17, 7,  21, 5  },
	{ 21, 23, 14, 13 },
	{ 22, 6,  15, 4  },
	{ 25, 5,  18, 24 },
	{ 18, 28, 14, 10 },
	{ 16, 15, 13, 10 },
	{ 16, 14, 11, 4  },
	{ 15, 14, 1,  7  },
	{ 9,  7,  26, 3  },
	{ 28, 13, 24, 12 },
	{ 26, 4,  23, 8  },
	{ 24, 1,  22, 0  },
	{ 23, 10, 9,  5  },
	{ 6,  11, 20, 0  },
	{ 21, 10, 19, 8  },
	{ 20, 1,  18, 12 },
	{ 5,  12, 29, 27 },
	{ 19, 4,  17, 3  },
	{ 3,  0,  29, 25 },
	{ 18, 13, 6,  2  },
	{ 8,  2,  25, 27 },
}};

export constexpr std::array<U8, 30> CARDS_INVERT = {
	10, 8, 7, 13, 12, 11, 9, 2, 1, 6, 0, 5, 4, 3, 27, 25, 29, 28, 26, 24, 23, 22, 21, 20, 19, 15, 18, 14, 17, 16
};

// Player `player` has card i in these indices of permutations.
export template<bool player>
constexpr auto P_HAS_CARD_IN = []{
	std::array<std::array<U8, 12>, 5> result{};
	std::array<U8, 5> counts{};
	for (U8 p = 0; p < CARDS_PERMUTATIONS.size(); p++) {
		for (U8 card : CARDS_PERMUTATIONS[p].playerCards[player])
			result[card][counts[card]++] = p;
	}
	return result;
}();

export template<bool player>
constexpr auto P_HAS_CARD_IN_MASK = [] {
	std::array<U32, 5> a{0};
	for (int i = 0; i < 5; i++)
		for (int j = 0; j < 12; j++)
			a[i] |= 1U << P_HAS_CARD_IN<player>[i][j];
	return a;
}();


export using MoveBoard = std::array<U32, 25>;

template<bool invert>
constexpr auto generateMoveBoard(const U32 card) {
	constexpr std::array<U32, 5> shiftMasks{
		0b11100'11100'11100'11100'11100,
		0b11110'11110'11110'11110'11110,
		0b11111'11111'11111'11111'11111,
		0b01111'01111'01111'01111'01111,
		0b00111'00111'00111'00111'00111,
	};
	U32 cardInverted = 0;
	for (U64 i = 0; i < 25; i++)
		cardInverted |= ((card >> i) & 1) << (invert ? 24 - i : i);

	MoveBoard moveBoard;
	for (U64 i = 0; i < 25; i++) {
		U32 maskedMove = cardInverted & shiftMasks[i % 5];
		moveBoard[i] = (i > 12 ? maskedMove << (i - 12) : maskedMove >> (12 - i)) & ((1ULL << 25) - 1);
	}
	return moveBoard;
}

export constexpr auto combineMoveBoards(const MoveBoard& a, const MoveBoard& b) {
	MoveBoard comb;
	for (U64 i = 0; i < 25; i++)
		comb[i] = a[i] | b[i];
	return comb;
}

export using CardSet = std::array<U32, 5>;

export struct MoveBoardSet {
	MoveBoard all;
	std::array<MoveBoard, 5> moveBoards;
};

template<bool invert>
constexpr auto generateMoveBoardSet(const CardSet& cards) {
	MoveBoardSet set;
	U32 all = 0;
	for (U64 i = 0; i < 5; i++) {
		all |= cards[i];
		set.moveBoards[i] = generateMoveBoard<invert>(cards[i]);
	}
	set.all = generateMoveBoard<invert>(all);
	return set;
}

export struct CardsInfo {
	CardSet cards;
	MoveBoardSet moveBoardsForward = generateMoveBoardSet<false>(cards);
	MoveBoardSet moveBoardsReverse = generateMoveBoardSet<true>(cards);
};


// TODO these are def wrong
export constexpr U32 p0_use_card0_unmasked(U32 entry) {
	return (entry >> 10) | (entry << 20);
}
export constexpr U32 p0_use_card1_unmasked(U32 entry) {
	return (entry >> 20) | (entry << 10);
}
export constexpr U32 p1_use_card0_unmasked(U32 entry) {
	return (entry >> 10) | (entry << 20);
}
export constexpr U32 p1_use_card1_unmasked(U32 entry) {
	return (entry >> 20) | (entry << 10);
}

// Per-square masks for the two 10-bit card slots in a table entry (TODO).
export constexpr auto P0_CARD0_USED_IN_MOVE_MASK = [] {
	constexpr U32 slot = (1U << 10) - 1;
	std::array<U32, 25> masks{};
	for (auto& m : masks)
		m = ~slot;
	return masks;
}();
export constexpr auto P0_CARD1_USED_IN_MOVE_MASK = [] {
	constexpr U32 slot = ((1U << 10) - 1) << 10;
	std::array<U32, 25> masks{};
	for (auto& m : masks)
		m = ~slot;
	return masks;
}();
export constexpr auto P1_CARD0_USED_IN_MOVE_MASK = [] {
	constexpr U32 slot = (1U << 10) - 1;
	std::array<U32, 25> masks{};
	for (auto& m : masks)
		m = ~slot;
	return masks;
}();
export constexpr auto P1_CARD1_USED_IN_MOVE_MASK = [] {
	constexpr U32 slot = ((1U << 10) - 1) << 10;
	std::array<U32, 25> masks{};
	for (auto& m : masks)
		m = ~slot;
	return masks;
}();
