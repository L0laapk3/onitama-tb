#include "Card.hpp"
#include "TableBase.h"

#include <iostream>



int main(int, char**) {
    
    constexpr CardsInfo CARDS{ BOAR, OX, ELEPHANT, HORSE, CRAB };
    generateTB(CARDS);

    std::cout << "done" << std::endl;
}
