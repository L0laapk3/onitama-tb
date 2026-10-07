export module tb:board;
import std;
import :types;
import :card;

export constexpr std::array<U32, 2> PTEMPLE = { 22, 2 };

export struct Board {
	std::array<U32, 2> bbp;
	std::array<U32, 2> bbk;

	static constexpr U32 isKingAttackedBy(U32 bbk, U32 bbp, const MoveBoard& reverseMoveBoard) {
		return reverseMoveBoard[std::countr_zero(bbk)] & bbp;
	}

	template <bool player>
	static constexpr bool isPlayerTempleEnded(const U32& bbk) {
		return bbk == (1U << PTEMPLE[player]);
	}
	constexpr bool isTempleEnded() const {
		return isPlayerTempleEnded<0>(bbk[0]) || isPlayerTempleEnded<1>(bbk[1]);
	}

	// is !player king safe?
	// is player attacking !players king?
	template <bool player>
	constexpr U32 isKingAttacked(U32 bbk, const MoveBoard& reverseMoveBoard) const {
		return isKingAttackedBy(bbk, bbp[player], reverseMoveBoard);
	}

	template <bool player>
	constexpr bool isTempleKingInRange(const MoveBoard& reverseMoveBoard) const {
		// player king can move to temple
		return reverseMoveBoard[PTEMPLE[player]] & bbk[player];
	}

	template <bool player>
	constexpr bool isTempleFree() const {
		// no player piece is blocking the temple.
		return !(bbp[player] & (1U << PTEMPLE[player]));
	}

	template <bool player>
	constexpr bool isTempleWinInOne(const MoveBoard& reverseMoveBoard) const {
		return isTempleKingInRange<player>(reverseMoveBoard) && isTempleFree<player>();
	}

	template <bool player>
	constexpr U32 isTakeWinInOne(const MoveBoard& reverseMoveBoard) const {
		return isKingAttacked<player>(bbk[!player], reverseMoveBoard);
	}

	template <bool player>
	constexpr bool isWinInOne(const MoveBoard& reverseMoveBoard) const {
		if (isTempleWinInOne<player>(reverseMoveBoard))
			return true;
		return isTakeWinInOne<player>(reverseMoveBoard);
	}

	// Cards with which pieces bbp can take the king bbk.
	static constexpr U32 takeWinCards(U32 bbk, U32 bbp, const MoveBoardSet& reverseMoveBoards) {
		U32 winCards = 0;
		for (int i = 0; i < 5; i++)
			if (isKingAttackedBy(bbk, bbp, reverseMoveBoards.moveBoards[i]))
				winCards |= P_HAS_CARD_IN_MASK<0>[i];
		return winCards;
	}

	// Cards with which `player`'s king bbk reaches its temple, ignoring whether the temple is free.
	template <bool player>
	static constexpr U32 templeKingCards(U32 bbk, const MoveBoardSet& reverseMoveBoards) {
		U32 winCards = 0;
		for (int i = 0; i < 5; i++)
			if (reverseMoveBoards.moveBoards[i][PTEMPLE[player]] & bbk)
				winCards |= P_HAS_CARD_IN_MASK<0>[i];
		return winCards;
	}

	// Entry bits are from the mover's perspective, so `player` holds playerCards[0].
	template <bool player>
	constexpr U32 getWinInOneCards(const MoveBoardSet& reverseMoveBoards) const {
		const U32 templeCards = isTempleFree<player>() ? templeKingCards<player>(bbk[player], reverseMoveBoards) : 0;
		return templeCards | takeWinCards(bbk[!player], bbp[player], reverseMoveBoards);
	};

	// debug utils
	void print() const;
	Board invert() const;
};

void Board::print() const {
	for (int r = 5; r-- > 0;) {
		for (int c = 0; c < 5; c++) {
			const U32 mask = 1U << (5 * r + c);
			if (bbp[0] & mask) {
				if ((bbp[1] | bbk[1]) & mask)  std::print("?");
				else if (bbk[0] & mask)        std::print("0");
				else                           std::print("o");
			} else if (bbp[1] & mask) {
				if (bbk[1] & mask)             std::print("X");
				else                           std::print("+");
			} else if ((bbk[0] | bbk[1]) & mask) std::print("F");
			else                                  std::print(".");
		}
		std::println("");
	}
	std::println("");
}

Board Board::invert() const {
	Board board{};
	for (U32 k = 0; k < 2; k++) {
		U32 bbprev = bbp[k];
		U32 bbkrev = bbk[k];
		for (U32 i = 0; i < 25; i++) {
			board.bbp[1 - k] = (board.bbp[1 - k] << 1) + (bbprev & 1);
			bbprev >>= 1;
			board.bbk[1 - k] = (board.bbk[1 - k] << 1) + (bbkrev & 1);
			bbkrev >>= 1;
		}
	}
	return board;
}