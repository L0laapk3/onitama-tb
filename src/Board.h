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

	static U64 inline isKingAttackedBy(U64 bbk, U64 bbp, const MoveBoard& reverseMoveBoard);
	template <bool player>
	U64 inline isKingAttacked(U64 bbk, const MoveBoard& reverseMoveBoard);
	template <bool player>
	bool inline isTempleKingInRange(const MoveBoard& reverseMoveBoard);
	template <bool player>
	bool inline isTempleFree();

	template <bool player>
	bool inline isTempleWinInOne(const MoveBoard& reverseMoveBoard);
	template <bool player>
	U64 inline isTakeWinInOne(const MoveBoard& reverseMoveBoard);
	template <bool player>
	bool inline isWinInOne(const MoveBoard& reverseMoveBoard);
	template <bool player>
	bool inline isWinInTwo(const MoveBoard& reverseMoveBoard, const MoveBoard& reverseOtherMoveBoard);
	template <bool player>
	bool inline isWinInThree(const MoveBoard& reverseMoveBoard, const MoveBoard& reverseMoveBoardcard0, const MoveBoard& reverseMoveBoardcard1, const MoveBoard& forwardOtherMoveBoard, const MoveBoard& reverseOtherMoveBoard);

	// debug utils
	void print() const;
	Board invert() const;
};


template <bool player>
bool inline Board::isTempleKingInRange(const MoveBoard& reverseMoveBoard) {
	// player king can move to temple
	return reverseMoveBoard[PTEMPLE[player]] & bbk[player];
}


template <bool player>
bool inline Board::isTempleFree() {
	// no player piece is blocking the temple.
	return !(bbp[player] & (1 << PTEMPLE[player]));
}


template <bool player>
bool inline Board::isTempleWinInOne(const MoveBoard& reverseMoveBoard) {
	return isTempleKingInRange<player>(reverseMoveBoard) && isTempleFree<player>();
}


U64 inline Board::isKingAttackedBy(U64 bbk, U64 bbp, const MoveBoard& reverseMoveBoard) {
	U64 pk = _tzcnt_u64(bbk);
	return reverseMoveBoard[pk] & bbp;
}

// is !player king safe?
// is player attacking !players king?
template <bool player>
U64 inline Board::isKingAttacked(U64 bbk, const MoveBoard& reverseMoveBoard) {
	return isKingAttackedBy(bbk, bbp[player], reverseMoveBoard);
}



template <bool player>
U64 inline Board::isTakeWinInOne(const MoveBoard& reverseMoveBoard) {
	return isKingAttacked<player>(bbk[!player], reverseMoveBoard);
}

template <bool player>
bool inline Board::isWinInOne(const MoveBoard& reverseMoveBoard) {
	if (isTempleWinInOne<player>(reverseMoveBoard))
		return true;
	return isTakeWinInOne<player>(reverseMoveBoard);
}


template <bool player>
bool inline Board::isWinInTwo(const MoveBoard& reverseMoveBoard, const MoveBoard& reverseOtherMoveBoard) {
	// it is the assumption that 2 is the first possible game-ending ply
	// !player cannot prevent player from winning
	if (isTempleWinInOne<player>(reverseMoveBoard)) // !player can never prevent temple wins without winning itself earlier
		return true;

	//TODO: if player pawn is on the temple square, threatening the !player king. and player king is threatening temple, !player cannot take it.

	// TODO: !player pieces can attack players pawns that threaten !player king!!!!!

	// temple player wins have been taken care of. All we have to do now is make sure !player has at least 1 move where king is safe

	// keep in mind the case that there are no valid pawn moves and the king is forced to move into a loss
	// TODO: we are just going to accept these false negatives and do the tb lookup on these for now, inspect perfect way later
	bool forcedToMove = false;

	if (!forcedToMove)
		if (!isKingAttacked<player>(bbk[!player], reverseMoveBoard))
			return false;

	// reverse moveboard cuz its the other player. check all forward king positions
	U64 possibleKingPositions = reverseOtherMoveBoard[_tzcnt_u64(bbk[!player])];
	while (possibleKingPositions) {
		if (!isKingAttacked<player>(possibleKingPositions, reverseMoveBoard))
			return false;
		possibleKingPositions &= possibleKingPositions - 1;
	}

	return true;
}


// check if board is a win in three. No need to check loss in 2 or win in 1, this is already checked earlier
template <bool player>
bool inline Board::isWinInThree(const MoveBoard& reverseMoveBoard, const MoveBoard& reverseMoveBoardcard0, const MoveBoard& reverseMoveBoardcard1, const MoveBoard& forwardOtherMoveBoard, const MoveBoard& reverseOtherMoveBoard) {
	// any player move for !player cannot prevent player from winning
	// it is the assumption that 3 is the first possible game-ending ply

	bool isKingThreatened = isTakeWinInOne<!player>(forwardOtherMoveBoard); // check if !player is threatening players king.

	
}