#pragma once

#include "Types.h"
#include "Card.hpp"

#include <array>
#include <x86intrin.h>
#include <xmmintrin.h>


constexpr std::array<U64, 2> PTEMPLE = { 22, 2 };

class Board {
public:
	std::array<U64, 2> bbp;
	std::array<U64, 2> bbk;

	template <U64 player>
	U64 inline isKingAttacked(U64 bbk, const MoveBoard& reverseMoveBoard);
	template <U64 player>
	bool inline isTempleKingInRange(const MoveBoard& reverseMoveBoard);
	template <U64 player>
	bool inline isTempleFree();

	template <U64 player>
	bool inline isTempleWinInOne(const MoveBoard& reverseMoveBoard);
	template <U64 player>
	U64 inline isTakeWinInOne(const MoveBoard& reverseMoveBoard);
	template <U64 player>
	bool inline isWinInOne(const MoveBoard& reverseMoveBoard);
	template <U64 player>
	bool inline isWinInTwo(const MoveBoard& reversePMoveBoard, const MoveBoard& reverseOtherPMoveBoard);
	template <U64 player>
	bool inline isWinInThree(const MoveBoard& reverseMoveBoard, const MoveBoard& reverseMoveBoardcard0, const MoveBoard& reverseMoveBoardcard1, const MoveBoard& forwardOtherPMoveBoard, const MoveBoard& reverseOtherPMoveBoard);

	// debug utils
	void print() const;
	Board invert() const;
};


template <U64 player>
bool inline Board::isTempleKingInRange(const MoveBoard& reverseMoveBoard) {
	// player king can move to temple
	return reverseMoveBoard[PTEMPLE[player]] & bbk[player];
}


template <U64 player>
bool inline Board::isTempleFree() {
	// no player piece is blocking the temple.
	return !(bbp[player] & (1 << PTEMPLE[player]));
}


template <U64 player>
bool inline Board::isTempleWinInOne(const MoveBoard& reverseMoveBoard) {
	return isTempleKingInRange<player>(reverseMoveBoard) && isTempleFree<player>();
}


template <U64 player>
U64 inline Board::isKingAttacked(U64 bbk, const MoveBoard& reverseMoveBoard) { // is !player king safe?
	U64 pk = _tzcnt_u64(bbk);
	return reverseMoveBoard[pk] & bbp[player];
}



template <U64 player>
U64 inline Board::isTakeWinInOne(const MoveBoard& reverseMoveBoard) {
	return isKingAttacked<player>(bbk[1-player], reverseMoveBoard);
}

template <U64 player>
bool inline Board::isWinInOne(const MoveBoard& reverseMoveBoard) {
	if (isTempleWinInOne<player>(reverseMoveBoard))
		return true;
	return isTakeWinInOne<player>(reverseMoveBoard);
}


template <U64 player>
bool inline Board::isWinInTwo(const MoveBoard& reversePMoveBoard, const MoveBoard& reverseOtherPMoveBoard) {
	// it is the assumption that 2 is the first possible game-ending ply
	// !player cannot prevent player from winning
	if (isTempleWinInOne<player>(reversePMoveBoard)) // !player can never prevent temple wins without winning itself earlier
		return true;

	// temple player wins have been taken care of. All we have to do now is make sure !player has at least 1 move where king is safe

	// keep in mind the case that there are no valid pawn moves and the king is forced to move into a loss
	// TODO: we are just going to accept these false negatives and do the tb lookup on these for now
	bool forcedToMove = false;

	if (!forcedToMove)
		if (!isKingAttacked<player>(bbk[1-player], reversePMoveBoard))
			return false;

	U64 possibleKingPositions = reverseOtherPMoveBoard[_tzcnt_u64(bbk[1-player])];
	while (possibleKingPositions) {
		if (!isKingAttacked<player>(possibleKingPositions, reversePMoveBoard))
			return false;
		possibleKingPositions &= possibleKingPositions - 1;
	}

	return true;
}


// optimisation idea: fast check if its even possible at all using precomputed tables
template <U64 player>
bool inline Board::isWinInThree(const MoveBoard& reverseMoveBoard, const MoveBoard& reverseMoveBoardcard0, const MoveBoard& reverseMoveBoardcard1, const MoveBoard& forwardOtherPMoveBoard, const MoveBoard& reverseOtherPMoveBoard) {
	// any player move for !player cannot prevent player from winning
	// it is the assumption that 3 is the first possible game-ending ply

	bool isKingThreatened = isTakeWinInOne<!player>(forwardOtherPMoveBoard); // check if !player is threatening players king.

	
}