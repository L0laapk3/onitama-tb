#pragma once

#include "Types.h"

#include <array>


constexpr U32 BOAR		= 0b00000'00100'01010'00000'00000;
constexpr U32 COBRA		= 0b00000'01000'00010'01000'00000;
constexpr U32 CRAB		= 0b00000'00100'10001'00000'00000;
constexpr U32 CRANE		= 0b00000'00100'00000'01010'00000;
constexpr U32 DRAGON	= 0b00000'10001'00000'01010'00000;
constexpr U32 EEL		= 0b00000'00010'01000'00010'00000;
constexpr U32 ELEPHANT	= 0b00000'01010'01010'00000'00000;
constexpr U32 FROG		= 0b00000'00010'00001'01000'00000;
constexpr U32 GOOSE		= 0b00000'00010'01010'01000'00000;
constexpr U32 HORSE		= 0b00000'00100'00010'00100'00000;
constexpr U32 MANTIS	= 0b00000'01010'00000'00100'00000;
constexpr U32 MONKEY	= 0b00000'01010'00000'01010'00000;
constexpr U32 OX		= 0b00000'00100'01000'00100'00000;
constexpr U32 RABBIT	= 0b00000'01000'10000'00010'00000;
constexpr U32 ROOSTER	= 0b00000'01000'01010'00010'00000;
constexpr U32 TIGER		= 0b00100'00000'00000'00100'00000;

struct CardPermutation {
	std::array<std::array<U8, 2>, 2> playerCards;
	U8 sideCard;
};

constexpr std::array<CardPermutation, 30> CARDS_PERMUTATIONS = {{
	{ 0, 1, 2, 3, 4 },
	{ 0, 2, 1, 3, 4 },
	{ 0, 3, 1, 2, 4 },
	{ 1, 2, 0, 3, 4 },
	{ 1, 3, 0, 2, 4 },
	{ 2, 3, 0, 1, 4 },
	{ 0, 1, 2, 4, 3 },
	{ 0, 2, 1, 4, 3 },
	{ 0, 4, 1, 2, 3 },
	{ 1, 2, 0, 4, 3 },
	{ 1, 4, 0, 2, 3 },
	{ 2, 4, 0, 1, 3 },
	{ 0, 1, 3, 4, 2 },
	{ 0, 3, 1, 4, 2 },
	{ 0, 4, 1, 3, 2 },
	{ 1, 3, 0, 4, 2 },
	{ 1, 4, 0, 3, 2 },
	{ 3, 4, 0, 1, 2 },
	{ 0, 2, 3, 4, 1 },
	{ 0, 3, 2, 4, 1 },
	{ 0, 4, 2, 3, 1 },
	{ 2, 3, 0, 4, 1 },
	{ 2, 4, 0, 3, 1 },
	{ 3, 4, 0, 2, 1 },
	{ 1, 2, 3, 4, 0 },
	{ 1, 3, 2, 4, 0 },
	{ 1, 4, 2, 3, 0 },
	{ 2, 3, 1, 4, 0 },
	{ 2, 4, 1, 3, 0 },
	{ 3, 4, 1, 2, 0 },
}};

constexpr std::array<std::array<std::array<U8, 2>, 2>, 30> CARDS_SWAP = {{
	{ 26, 20, 12, 6  },
	{ 28, 14, 18, 7  },
	{ 29, 8,  19, 13 },
	{ 22, 16, 24, 9  },
	{ 23, 10, 25, 15 },
	{ 17, 11, 27, 21 },
	{ 25, 19, 12, 0  },
	{ 27, 13, 18, 1  },
	{ 29, 2,  20, 14 },
	{ 21, 15, 24, 3  },
	{ 23, 4,  26, 16 },
	{ 17, 5,  28, 22 },
	{ 24, 18, 6,  0  },
	{ 27, 7,  19, 2  },
	{ 28, 1,  20, 8  },
	{ 21, 9,  25, 4  },
	{ 22, 3,  26, 10 },
	{ 11, 5,  29, 23 },
	{ 24, 12, 7,  1  },
	{ 25, 6,  13, 2  },
	{ 26, 0,  14, 8  },
	{ 15, 9,  27, 5  },
	{ 16, 3,  28, 11 },
	{ 10, 4,  29, 17 },
	{ 18, 12, 9,  3  },
	{ 19, 6,  15, 4  },
	{ 20, 0,  16, 10 },
	{ 13, 7,  21, 5  },
	{ 14, 1,  22, 11 },
	{ 8,  2,  23, 17 },
}};


constexpr std::array<U8, 30> CARDS_INVERT = { 5, 4, 3, 2, 1, 0, 11, 10, 9, 8, 7, 6, 17, 16, 15, 14, 13, 12, 23, 22, 21, 20, 19, 18, 29, 28, 27, 26, 25, 24 };

constexpr std::array<std::array<U8, 12>, 5> CARDS_USED_IN = {{
	{ 0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20 },
	{ 0, 3, 4, 6, 9, 10, 12, 15, 16, 24, 25, 26 },
	{ 1, 3, 5, 7, 9, 11, 18, 21, 22, 24, 27, 28 },
	{ 2, 4, 5, 13, 15, 17, 19, 21, 23, 25, 27, 29 },
	{ 8, 10, 11, 14, 16, 17, 20, 22, 23, 26, 28, 29 },
}};

constexpr std::array<U8, 10> CARDS_P0_PAIRS = {0, 1, 2, 3, 4, 5, 8, 10, 11, 17 };



typedef std::array<U32, 25> MoveBoard;

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

constexpr auto combineMoveBoards(const MoveBoard& a, const MoveBoard& b) {
	MoveBoard comb;
    for (U64 i = 0; i < 25; i++)
		comb[i] = a[i] | b[i];
	return comb;
}




typedef std::array<U32, 5> CardSet;
typedef std::array<MoveBoard, 5> MoveBoardSet;

template<bool invert>
constexpr auto generateMoveBoardSet(const CardSet& cards) {
	MoveBoardSet moveBoards;
	for (U64 i = 0; i < 5; i++)
		moveBoards[i] = generateMoveBoard<invert>(cards[i]);
    return moveBoards;
}



struct CardsInfo {
	CardSet cards;
	MoveBoardSet moveBoardsForward = generateMoveBoardSet<false>(cards);
	MoveBoardSet moveBoardsReverse = generateMoveBoardSet<true>(cards);
};


void print(const MoveBoard& moves);