#include "Card.hpp"
#include "TableBase.h"

#include <iostream>



int main(int, char**) {

    if (0) {
        testIndexing();
        return 0;
    } else if (1) {
        constexpr CardsInfo CARDS{ BOAR, OX, ELEPHANT, HORSE, CRAB };
        generateTB<true>(CARDS);
    } else {
		benchTB(10);
	}
}
