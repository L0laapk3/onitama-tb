#include "Index.h"

#include <utility>
#include <cassert>
#include "x86intrin.h"
#include <xmmintrin.h>
#include <iostream>
#include <bitset>
#include <algorithm>





void iterateTBCounts(const MoveBoard& reverseMoveBoard, std::function<void(U32, U32)> cb) {
	for (U64 pieceCountI = 0; pieceCountI < PIECECOUNTMULT; pieceCountI++) {
		auto& pc = OFFSET_ORDER[pieceCountI];
		for (U64 kingI = 0; kingI < KINGSMULT; kingI++) {
			U32 pieceCnt_kingsIndex = pieceCountI * KINGSMULT + kingI;
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

			cb(pieceCnt_kingsIndex, rowSize);
		}
	}
}






template <bool startInv, bool endInv>
void testOne(const CardsInfo& cards) {

	BoardIndex bi;
	for (U64 cardI = 0; cardI < CARDSMULT; cardI++) {
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsReverse[permutation.playerCards[0][0]], cards.moveBoardsReverse[permutation.playerCards[0][1]]);
		const MoveBoard forwardOtherMoveBoard = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[0][0]], cards.moveBoardsForward[permutation.playerCards[0][1]]);
		const auto& startMoveBoard = startInv ? forwardOtherMoveBoard : reverseMoveBoard;
		const auto& endMoveBoard = endInv ? forwardOtherMoveBoard : reverseMoveBoard;

		iterateTBCounts(reverseMoveBoard, [&](U32 pieceCnt_kingsIndex, U32 rowSize) {
			// std::cout << cardI << '\t' << pieceCountI << '\t' << kingI << " (" << _tzcnt_u64(bbk0) << ' ' << ik1 << ")\t" << p0Combinations << std::endl;

			for (bi.pieceIndex = 0; bi.pieceIndex < rowSize; bi.pieceIndex++) {
				Board board = indexToBoard<startInv>(bi, startMoveBoard);
				if (board.isWinInOne<startInv>(startMoveBoard)) {
					board.print();
					std::cout << "index resolves to win in 1: (" << bi.pieceCnt_kingsIndex << " " << bi.pieceIndex << ")" << std::endl;
					board.isWinInOne<startInv>(startMoveBoard);
					indexToBoard<startInv>(bi, startMoveBoard);
				}
				board.bbp[0] &= (1ULL << 25) - 1;
				board.bbp[1] &= (1ULL << 25) - 1;
				if (startInv != endInv)
					board = board.invert();
				auto result = boardToIndex<endInv>(board, endMoveBoard);
				if (result.pieceCnt_kingsIndex != bi.pieceCnt_kingsIndex || result.pieceIndex != bi.pieceIndex) {
					std::cout << "problem (" << bi.pieceCnt_kingsIndex << " " << bi.pieceIndex << "), (" << result.pieceCnt_kingsIndex << " " << result.pieceIndex << ")" << std::endl;
					Board board2 = indexToBoard<startInv>(bi, startMoveBoard);
					if (startInv != endInv)
						board2 = board2.invert();
					boardToIndex<endInv>(board2, endMoveBoard);
				}
			}
		});
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




// hardcoded 6 men test
void exhaustiveIndexTest(const CardsInfo& cards) {
	U64 count = 0;
	U64 noWinIn1Count = 0;
	for (U64 cardI = 0; cardI < CARDSMULT; cardI++) {
		auto permutation = CARDS_PERMUTATIONS[cardI];
		const MoveBoard reverseMoveBoard = combineMoveBoards(cards.moveBoardsForward[permutation.playerCards[0][0]], cards.moveBoardsForward[permutation.playerCards[0][1]]);

		for (U64 bbk0 = 1ULL; bbk0 < 1ULL << 25; bbk0 <<= 1)
			if (bbk0 != 1ULL << PTEMPLE[0])
				for (U64 bbk1 = 1ULL; bbk1 < 1ULL << 25; bbk1 <<= 1)
					if ((bbk0 != bbk1) && (bbk1 != 1ULL << PTEMPLE[1]))
						for (U64 bbp00 = 1ULL; bbp00 < 1ULL << 26; bbp00 <<= 1)
							if (bbp00 != bbk0 && bbp00 != bbk1)
								for (U64 bbp01 = std::min(bbp00 << 1, 1ULL << 25); bbp01 < 1ULL << 26; bbp01 <<= 1)
									if (bbp01 != bbk0 && bbp01 != bbk1) {
										U64 bbp0 = (bbp00 | bbp01 | bbk0) & ((1ULL << 25) - 1);
										for (U64 bbp10 = 1ULL; bbp10 < 1ULL << 26; bbp10 <<= 1)
											if ((bbp10 != bbk1) && (bbp10 & bbp0) == 0)
												for (U64 bbp11 = std::min(bbp10 << 1, 1ULL << 25); bbp11 < 1ULL << 26; bbp11 <<= 1)
													if ((bbp11 != bbk1) && (bbp11 & bbp0) == 0) {
														U64 bbp1 = (bbp10 | bbp11 | bbk1) & ((1ULL << 25) - 1);

														Board board {
															.bbp = { bbp0, bbp1 },
															.bbk = { bbk0, bbk1 },
														};

														if (!board.isWinInOne<true>(reverseMoveBoard)) {
															auto bi = boardToIndex<true>(board, reverseMoveBoard);
															auto result = indexToBoard<true>(bi, reverseMoveBoard);
															if (result.bbp[0] != board.bbp[0] || result.bbp[1] != board.bbp[1] || result.bbk[0] != board.bbk[0] || result.bbk[1] != board.bbk[1]) {
																std::cout << "problem: " << bi.pieceCnt_kingsIndex << ' ' << bi.pieceIndex << std::endl;
																board.print();
																std::cout << std::endl;
																result.print();
																std::cout << std::endl;
																if (result.isWinInOne<true>(reverseMoveBoard))
																	std::cout << "double wtf" << std::endl;
																boardToIndex<true>(board, reverseMoveBoard);
																indexToBoard<true>(bi, reverseMoveBoard);
															}
															noWinIn1Count++;
														}
														count++;
													}
									}
	}
	
	std::cout << "test finished! Tested " << count << " boards, " << noWinIn1Count << " were no win in 1" << std::endl;
}