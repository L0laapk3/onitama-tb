#include "Card.hpp"

#include <iostream>


void print(MoveBoard moves) {
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