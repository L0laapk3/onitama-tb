#include "Index.h"

#include <utility>
#include <cassert>
#include "x86intrin.h"
#include <xmmintrin.h>
#include <iostream>
#include <bitset>
#include <algorithm>


template <bool startInv, bool endInv>
void testOne(const CardsInfo& cards) {

	BoardIndex bi;
	for (U64 cardI = 0; cardI < CARDSMULT; cardI++) {
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard forwardOtherMoveBoard = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[0][0]], cards.moveBoardsForward[permutation.playerCards[0][1]]);
		const auto& startMoveBoard = startInv ? forwardOtherMoveBoard : reverseMoveBoard;
		const auto& endMoveBoard = endInv ? forwardOtherMoveBoard : reverseMoveBoard;
		for (U64 pieceCountI = 0; pieceCountI < PIECECOUNTMULT; pieceCountI++) {
			auto& pc = OFFSET_ORDER[pieceCountI];
			for (U64 kingI = 0; kingI < KINGSMULT; kingI++) {
				bi.pieceCnt_KingsIndex = pieceCountI * KINGSMULT + kingI;
				U32 rowSize;

				U64 bbk0, bbk1;
				std::tie(bbk0, bbk1) = TABLES_BBKINGS[0][kingI];
				U64 ik1 = _tzcnt_u64(bbk1);

				U64 p0mask = bbk0 | bbk1 | reverseMoveBoard[ik1];

				if (reverseMoveBoard[ik1] & bbk0)
					rowSize = 0;
				else {
					bool templeWinThreatened = reverseMoveBoard[PTEMPLE[0]] & bbk0;
					if (templeWinThreatened && ((pc.first == 0) || (reverseMoveBoard[ik1] & (1 << PTEMPLE[0]))))
						rowSize = 0;
					else {
						if (templeWinThreatened)
							p0mask |= 1 << PTEMPLE[0];

						U64 p0Options = 25 - _popcnt64(p0mask);
						U64 p0Combinations = templeWinThreatened && !pc.first ? 0 : fact(p0Options, p0Options-(pc.first-templeWinThreatened)) / fact(pc.first-templeWinThreatened);
						U64 p1Combinations = fact(23-pc.first, 23-pc.first-pc.second) / fact(pc.second);
						rowSize = p0Combinations * p1Combinations;
					}
				}
				// std::cout << cardI << '\t' << pieceCountI << '\t' << kingI << " (" << _tzcnt_u64(bbk0) << ' ' << ik1 << ")\t" << p0Combinations << std::endl;

				for (bi.pieceIndex = 0; bi.pieceIndex < rowSize; bi.pieceIndex++) {
					Board board = indexToBoard<startInv>(bi, startMoveBoard);
					if (board.isWinInOne<startInv>(startMoveBoard)) {
						board.print();
						std::cout << "index resolves to win in 1: (" << bi.pieceCnt_KingsIndex << " " << bi.pieceIndex << ")" << std::endl;
						board.isWinInOne<startInv>(startMoveBoard);
						indexToBoard<startInv>(bi, startMoveBoard);
					}
					board.bbp[0] &= (1ULL << 25) - 1;
					board.bbp[1] &= (1ULL << 25) - 1;
					if (startInv != endInv)
						board = board.invert();
					auto result = boardToIndex<endInv>(board, endMoveBoard);
					if (result.pieceCnt_KingsIndex != bi.pieceCnt_KingsIndex || result.pieceIndex != bi.pieceIndex) {
						std::cout << "problem (" << bi.pieceCnt_KingsIndex << " " << bi.pieceIndex << "), (" << result.pieceCnt_KingsIndex << " " << result.pieceIndex << ")" << std::endl;
						Board board2 = indexToBoard<startInv>(bi, startMoveBoard);
						if (startInv != endInv)
							board2 = board2.invert();
						boardToIndex<endInv>(board2, endMoveBoard);
					}
				}
			}
		}
	}
}


void testIndexing(const CardsInfo& cards) {
	
	std::cout << "testing normal to normal" << std::endl;
	testOne<false, false>(cards);

	std::cout << "testing inverted to inverted" << std::endl;
	testOne<true, true>(cards);

	std::cout << "testing normal to inverted" << std::endl;
	testOne<false, true>(cards);

	std::cout << "testing inverted to normal" << std::endl;
	testOne<true, false>(cards);

	std::cout << "test finished!" << std::endl;
				
}