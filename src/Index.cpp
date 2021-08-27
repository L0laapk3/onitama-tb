#include "Index.h"

#include <utility>
#include <cassert>
#include "x86intrin.h"
#include <xmmintrin.h>
#include <iostream>
#include <bitset>
#include <algorithm>




void testIndexing() {

	// std::cout << index << " " << Board::toIndex<invert>({
	// 	.bbp0 = bbp0,
	// 	.bbp1 = bbp1,
	// 	.bbk0 = bbk0,
	// 	.bbk1 = bbk1,
	// }) << std::endl;
	
	std::cout << "testing normal to normal" << std::endl;
	for (U64 i = 0; i < TB_ROW_SIZE; i++)
		if (i != boardToIndex<false>(indexToBoard<false>(i))) {
			std::cout << "problem normal to normal " << i << std::endl;
			boardToIndex<false>(indexToBoard<false>(i));
		}
	
	std::cout << "testing inverted to inverted" << std::endl;
	for (U64 i = 0; i < TB_ROW_SIZE; i++)
		if (i != boardToIndex<true>(indexToBoard<true>(i))) {
			indexToBoard<false>(i).print();
			indexToBoard<true>(i).print();
			std::cout << "problem inverted to inverted " << i << std::endl;
			boardToIndex<true>(indexToBoard<true>(i));
		}
	
	std::cout << "testing normal to inverted" << std::endl;
	for (U64 i = 0; i < TB_ROW_SIZE; i++)
		if (i != boardToIndex<true>(indexToBoard<false>(i).invert())) {
			std::cout << "problem normal to inverted " << i << std::endl;
			boardToIndex<true>(indexToBoard<false>(i).invert());
		}
	
	std::cout << "testing inverted to normal" << std::endl;
	for (U64 i = 0; i < TB_ROW_SIZE; i++)
		if (i != boardToIndex<false>(indexToBoard<true>(i).invert())) {
			std::cout << "problem inverted to normal " << i << std::endl;
			boardToIndex<false>(indexToBoard<true>(i).invert());
		}
}