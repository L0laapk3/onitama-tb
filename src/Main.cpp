#include "Card.hpp"
#include "TableBase.h"

#include <iostream>



int main(int, char**) {
	const CardsInfo CARDS{ BOAR, OX, ELEPHANT, HORSE, CRAB };

    if (1) {
        testIndexing(CARDS);
        return 0;
    } else {
        generateTB(CARDS);
    }
}
