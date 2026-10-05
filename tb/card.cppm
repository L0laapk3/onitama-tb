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

// Card bits of a table entry, playerCards[0] is the player to move. Bit 10 * k + h: the 10 bits h share
// the mover's hand, k picks which of the 3 remaining cards is the side card. So rotating by 10 or 20 changes
// only the side card, and swapping the hands is a permutation within each group of 6 bits (swapPlayers).
export constexpr std::array<CardPermutation, 30> CARDS_PERMUTATIONS = {{
	{ 2, 3, 1, 4, 0 },
	{ 2, 4, 1, 3, 0 },
	{ 0, 4, 1, 2, 3 },
	{ 1, 4, 2, 3, 0 },
	{ 1, 2, 0, 4, 3 },
	{ 1, 3, 2, 4, 0 },
	{ 3, 4, 0, 2, 1 },
	{ 0, 3, 2, 4, 1 },
	{ 0, 1, 2, 3, 4 },
	{ 0, 2, 3, 4, 1 },
	{ 2, 3, 0, 1, 4 },
	{ 2, 4, 0, 3, 1 },
	{ 0, 4, 1, 3, 2 },
	{ 1, 4, 0, 3, 2 },
	{ 1, 2, 3, 4, 0 },
	{ 1, 3, 0, 4, 2 },
	{ 3, 4, 1, 2, 0 },
	{ 0, 3, 1, 4, 2 },
	{ 0, 1, 2, 4, 3 },
	{ 0, 2, 1, 4, 3 },
	{ 2, 3, 0, 4, 1 },
	{ 2, 4, 0, 1, 3 },
	{ 0, 4, 2, 3, 1 },
	{ 1, 4, 0, 2, 3 },
	{ 1, 2, 0, 3, 4 },
	{ 1, 3, 0, 2, 4 },
	{ 3, 4, 0, 1, 2 },
	{ 0, 3, 1, 2, 4 },
	{ 0, 1, 3, 4, 2 },
	{ 0, 2, 1, 3, 4 },
}};

constexpr U8 findCardPermutation(std::array<U8, 2> hand0, std::array<U8, 2> hand1, U8 side) {
	const auto sameHand = [](std::array<U8, 2> a, std::array<U8, 2> b) {
		return (a[0] == b[0] && a[1] == b[1]) || (a[0] == b[1] && a[1] == b[0]);
	};
	for (U8 p = 0; p < CARDS_PERMUTATIONS.size(); p++) {
		const auto& perm = CARDS_PERMUTATIONS[p];
		if (sameHand(perm.playerCards[0], hand0) && sameHand(perm.playerCards[1], hand1) && perm.sideCard == side)
			return p;
	}
	throw "card permutation not found";
}

// [perm][player][slot]: the permutation after `player` used its card in `slot`.
export constexpr auto CARDS_SWAP = [] {
	std::array<std::array<std::array<U8, 2>, 2>, 30> result{};
	for (U8 p = 0; p < CARDS_PERMUTATIONS.size(); p++) {
		const auto& perm = CARDS_PERMUTATIONS[p];
		for (int player = 0; player < 2; player++) {
			for (int slot = 0; slot < 2; slot++) {
				auto hands = perm.playerCards;
				hands[player][slot] = perm.sideCard;
				result[p][player][slot] = findCardPermutation(hands[0], hands[1], perm.playerCards[player][slot]);
			}
		}
	}
	return result;
}();

export constexpr auto CARDS_INVERT = [] {
	std::array<U8, 30> result{};
	for (U8 p = 0; p < CARDS_PERMUTATIONS.size(); p++) {
		const auto& perm = CARDS_PERMUTATIONS[p];
		result[p] = findCardPermutation(perm.playerCards[1], perm.playerCards[0], perm.sideCard);
	}
	return result;
}();

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

export constexpr U32 CARD_PERMS_MASK = (1U << 30) - 1;

// Permutations in which card i is the side card.
export constexpr auto SIDE_CARD_MASK = [] {
	std::array<U32, 5> a{};
	for (U8 p = 0; p < CARDS_PERMUTATIONS.size(); p++)
		a[CARDS_PERMUTATIONS[p].sideCard] |= 1U << p;
	return a;
}();

// Same mover's hand, other side card.
export constexpr U32 otherSideCards(U32 bits) {
	const U64 rotated = (U64(bits) << 10) | (U64(bits) << 20);
	return static_cast<U32>(rotated | (rotated >> 30)) & CARD_PERMS_MASK;
}

// Same as CARDS_INVERT, but on the bits of an entry.
export constexpr U32 swapPlayers(U32 bits) {
	constexpr U32 TRIPLE_LOW = 0b001001'001001'001001'001001'001001;
	constexpr U32 HALF_LOW = 0b000111'000111'000111'000111'000111;
	bits = (bits & TRIPLE_LOW) | ((bits & TRIPLE_LOW << 1) << 1) | ((bits & TRIPLE_LOW << 2) >> 1);
	return ((bits & HALF_LOW) << 3) | ((bits & HALF_LOW << 3) >> 3);
}

// Given an input card entry with these bits, return the bits that can move into one of these moves.
export constexpr U32 unmoveCardEntry(U32 after) {
	return swapPlayers(otherSideCards(after));
}
// Given an input card entry with these bits, return the bits that are the result of any move (with either card)
export constexpr U32 moveCardEntry(U32 after) {
	// return swapPlayers(otherSideCards(after));
	return after; // TODO
}

static_assert([] {
	for (U8 p = 0; p < CARDS_PERMUTATIONS.size(); p++) {
		if (swapPlayers(1U << p) != 1U << CARDS_INVERT[p])
			return false;
		const auto& perm = CARDS_PERMUTATIONS[p];
		for (int slot = 0; slot < 2; slot++) {
			const U8 card = perm.playerCards[0][slot];
			const U8 after = CARDS_INVERT[CARDS_SWAP[p][0][slot]];
			const U32 before = unmoveCardEntry((1U << after) & SIDE_CARD_MASK[card]);
			if (!(before & (1U << p)) || std::popcount(before) != 2 || (before & ~P_HAS_CARD_IN_MASK<0>[card]))
				return false;
		}
	}
	return true;
}());


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
	// [from][to]: SIDE_CARD_MASK of all cards that make this move.
	std::array<std::array<U32, 25>, 25> sideCards;
};

template<bool invert>
constexpr auto generateMoveBoardSet(const CardSet& cards) {
	MoveBoardSet set{};
	U32 all = 0;
	for (U64 i = 0; i < 5; i++) {
		all |= cards[i];
		set.moveBoards[i] = generateMoveBoard<invert>(cards[i]);
		for (U64 from = 0; from < 25; from++)
			for (U64 to = 0; to < 25; to++)
				if (set.moveBoards[i][from] & (1U << to))
					set.sideCards[from][to] |= SIDE_CARD_MASK[i];
	}
	set.all = generateMoveBoard<invert>(all);
	return set;
}

export struct CardsInfo {
	CardSet cards;
	MoveBoardSet moveBoardsForward = generateMoveBoardSet<false>(cards);
	MoveBoardSet moveBoardsReverse = generateMoveBoardSet<true>(cards);
};
