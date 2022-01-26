#include "Card.hpp"

#include <iostream>
#include <vector>
#include <algorithm>


void print(const MoveBoard& moves) {
	for (U64 j = 0; j < 25; j += 5) {
		for (int r = 5; r-- > 0;) {
			for (U64 i = j; i < j + 5; i++) {
				for (int c = 0; c < 5; c++) {
					const int mask = 1 << (5 * r + c);
					std::cout << ((moves[i] & mask) ? 'o' : '-');
				}
				std::cout << ' ';
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;
	}
}



void iterateCardCombinations(std::function<void(const CardsInfo&, const std::array<U32, 5>&)> cb) {
    std::vector<bool> v(16, false);
    std::fill(v.begin(), v.begin() + 5, true);
    do {
		U64 cardsFilled = 0;
		std::array<U32, 5> cards;
		std::array<U32, 5> cardsIndexes;
        for (int i = 0; i < 16; ++i) {
            if (v[i]) {
                cardsIndexes[cardsFilled] = i;
				cards[cardsFilled++] = ALL_CARDS[i];
            }
        }
		CardsInfo cardsInfo{ cards };
		cb(cardsInfo, cardsIndexes);
    } while (std::prev_permutation(v.begin(), v.end()));
}