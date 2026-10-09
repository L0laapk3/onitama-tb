module;
#include <cerrno>
#include <sys/mman.h>
#include "inline.h"
export module tb:tablebase;
import std;
import :types;
import :card;
import :board;
import :index;
import :sync;

export template <U16 TB_MEN>
struct TableBase;

struct ThreadObj {
	Sync sync;
	U64 iteration = 0;
	bool exit = false;
	std::size_t group = 0; // index into PIECE_COUNT_ORDER
	std::atomic<bool> updated = true;
};

constexpr U64 CHUNK_P0_POSITIONS = 1;
constexpr bool VERBOSE = false;

__FORCE_INLINE U32 fetch_mask(std::atomic<U32>& entry, U32 mask, std::memory_order order) {
	auto value = entry.load(std::memory_order_relaxed);
	if (value & mask)
		value = entry.fetch_and(~mask, order);
	return value & ~mask;
}

// STEP 1: no TableBase lookups in the forward movegen, it checks the children for win in 0/1 directly.
// STEP 2: TableBase lookups.
template <U16 TB_MEN, U16 P0C, U16 P1C, int STEP>
void processRow(const CardsInfo& cards, auto& tb, auto& landings, U64& chunk, U64& rowStartChunk, std::atomic<U64>& chunkCounter, bool& updated) {
	auto& row = tb.template getRow<P0C, P1C>();
	auto& landingsRow = landings.template getRow<P0C, P1C>();

	// Iterate in mirrored <P1C, P0C> order: the outer loop fixes p1's pieces, so all non-take children share the mirrored row at ip0_new.
	constexpr U32 OUTER_SIZE = PAWNTABLE_P0<P1C, P0C>.size();
	constexpr U32 INNER_SIZE = PAWNTABLE_P1<P1C, P0C>.size();
	constexpr U64 P0_CHUNKS = (OUTER_SIZE - 1) / CHUNK_P0_POSITIONS + 1;

	for (; chunk < rowStartChunk + P0_CHUNKS; chunk = chunkCounter++) {
		const U32 begin = static_cast<U32>((chunk - rowStartChunk) * CHUNK_P0_POSITIONS);
		const U32 end = std::min<U32>(begin + CHUNK_P0_POSITIONS, OUTER_SIZE);
		for (int ip0_new = begin; ip0_new < end; ip0_new++) { // loop over the outer table in an order that maximizes locality for forward move generation
			const U32 bbp1 = unrankFirstPieces<true, P1C, P0C>(ip0_new);

			std::array<int, P1C> ip0s_taken; // precalculate all the ranks for different taken pieces
			if constexpr (P1C > 1) {
				U32 bbp1_source = bbp1;
				for (int i = 0; i < P1C; i++) {
					const U32 pp1 = bbp1_source & -bbp1_source;
					bbp1_source &= bbp1_source - 1;
					ip0s_taken[i] = rankFirstPieces<true, P1C - 1, P0C>(bbp1 - pp1);
				}
			}
			std::array<U64, P1C> childTempleSplits; // per ik1: unmoveSplit of the child's temple win cards, if p1's temple is free
			if constexpr (STEP == 1)
				for (int ik1 = 0; ik1 < P1C; ik1++)
					childTempleSplits[ik1] = unmoveSplit(Board::templeKingCards<1>(unrankSecondKing<false, P0C, P1C>(ik1, bbp1), cards.moveBoardsForward));
			auto* landingsIt = landingsRow[ip0_new].data();
			for (int ipInner = 0; ipInner < static_cast<int>(INNER_SIZE); ipInner++, landingsIt++) {
				U32& unresolvedLandings = *landingsIt;
				if (STEP > 1 && !unresolvedLandings)
					continue;
				const U32 bbp0 = unrankSecondPieces<true, P1C, P0C>(ipInner, bbp1);
				auto& rowP1 = row[rankFirstPieces<false, P0C, P1C>(bbp0)][rankSecondPieces<false, P0C, P1C>(bbp1, bbp0)];
				auto* it = &rowP1[0][0];
				__builtin_prefetch(it, 1, 3);
				if (ipInner + 3 < static_cast<int>(INNER_SIZE)) {
					const U32 bbp0Next = unrankSecondPieces<true, P1C, P0C>(ipInner + 3, bbp1);
					auto& rowP1Next = row[rankFirstPieces<false, P0C, P1C>(bbp0Next)][rankSecondPieces<false, P0C, P1C>(bbp1, bbp0Next)];
					__builtin_prefetch(&rowP1Next[0][0], 1, 3);
				}
				std::array<U32, P0C * P1C> entries;
				U32 unresolvedUnion = 0;

				{ // Optimization: If the entire block of king perms is empty, continue early
					// every entry has to be read: the forward movegen uses them. A stored bit of 1 means that card perm is still unresolved.
					for (int i = 0; i < P0C * P1C; i++) {
						entries[i] = it[i].load(std::memory_order_relaxed);
						unresolvedUnion |= entries[i];
					}
					if (!unresolvedUnion) {
						unresolvedLandings = 0;
						continue;
					}
				}

				const std::array<U32, P0C * P1C> startEntries = entries;
				const U32 startUnion = unresolvedUnion;
				std::array<U32, P0C * P1C> startEntriesMoved; // child card perms that help a start entry
				if constexpr (STEP > 1)
					for (int i = 0; i < P0C * P1C; i++)
						startEntriesMoved[i] = moveCardEntry(startEntries[i]);
				U32 newUnresolvedLandings = 0;
				// From here on, entries only keeps the unresolved card perms that have no move to an unresolved child yet.
				{ // forwards movegen - check if all possible p0 moves are resolved
					std::array<U64, P0C> childTakeSplits; // per ik0: unmoveSplit of the child's take win cards when neither the king moves nor a piece is taken
					if constexpr (STEP == 1)
						for (int ik0 = 0; ik0 < P0C; ik0++)
							childTakeSplits[ik0] = unmoveSplit(Board::takeWinCards(unrankFirstKing<false, P0C, P1C>(ik0, bbp0), bbp1, cards.moveBoardsForward));

					// STEP > 1: prefetch all children up front. unresolvedUnion only shrinks, so these landings are a superset of the ones evaluated below.
					std::array<U32, P0C> prefetchedLandings;
					std::array<const std::atomic<U32>*, P0C * (25 - P0C)> childRows; // flat king blocks, rows of P0C, per source then landing
					std::array<U8, P0C> childRowsStart;
					if constexpr (STEP > 1) {
						auto childIt = childRows.begin();
						U32 sourcePieces = bbp0;
						for (int iSrc = 0; iSrc < P0C; iSrc++) {
							const U32 sourcePiece = sourcePieces & -sourcePieces;
							const int pp = std::countr_zero(sourcePieces);
							sourcePieces &= sourcePieces - 1;
							childRowsStart[iSrc] = static_cast<U8>(childIt - childRows.begin());
							U32 landPieces = moveBoardFromCardEntry(cards.moveBoardsForward.moveBoards, unresolvedUnion, pp) & unresolvedLandings & ~bbp0;
							prefetchedLandings[iSrc] = landPieces;
							while (landPieces) {
								const U32 landPiece = landPieces & -landPieces;
								landPieces &= landPieces - 1;
								const U32 bbp0_new = (bbp0 - sourcePiece) | landPiece;
								const std::atomic<U32>* childRow;
								if constexpr (P1C > 1) {
									if (landPiece & bbp1) {
										const int iTaken = std::popcount(bbp1 & (landPiece - 1));
										childRow = &tb.template getRow<P1C - 1, P0C>()[ip0s_taken[iTaken]][rankSecondPieces<true, P1C - 1, P0C>(bbp0_new, bbp1 & ~landPiece)][0][0];
									} else
										childRow = &tb.template getRow<P1C, P0C>()[ip0_new][rankSecondPieces<true, P1C, P0C>(bbp0_new, bbp1)][0][0];
								} else
									childRow = &tb.template getRow<P1C, P0C>()[ip0_new][rankSecondPieces<true, P1C, P0C>(bbp0_new, bbp1)][0][0];
								__builtin_prefetch(childRow, 0, 3);
								*childIt++ = childRow;
							}
						}
					}

					U32 sourcePieces = bbp0;
					for (int iSrc = 0; iSrc < P0C; iSrc++) {
						const U32 sourcePiece = sourcePieces & -sourcePieces;
						int pp = std::countr_zero(sourcePieces);
						sourcePieces &= sourcePieces - 1;
						const U32 bbp0_without_source = bbp0 - sourcePiece;
						U32 landPieces = moveBoardFromCardEntry(cards.moveBoardsForward.moveBoards, unresolvedUnion, pp); // Its possible that in the future, its faster again to only calculate these once per forwards movegen.
						newUnresolvedLandings |= moveBoardFromCardEntry(cards.moveBoardsForward.moveBoards, startUnion, pp) & ~landPieces; // skipped by the card filter, may still help the start entries
						if constexpr (STEP > 1)
							landPieces &= unresolvedLandings;
						landPieces &= ~bbp0; // lookup unresolved landings & can't land on my own pieces
						std::array<U32, P0C * P1C> childHelpers{}; // unresolved child card perms of this source's moves, unmoved once after the landings loop
						while (landPieces) {
							const U32 landPiece = landPieces & -landPieces;
							landPieces &= landPieces - 1;
							const U32 bbp0_new = bbp0_without_source | landPiece;

							if constexpr (STEP == 1) {
								const U32 bbp1_new = bbp1 & ~landPiece;
								const bool isTakeMove = landPiece & bbp1;
								const U64 templeMask = bbp1_new & (1U << PTEMPLE[1]) ? 0 : ~0ULL;
								const U64 sideSplit = cards.moveBoardsForward.unmoveSideCards[pp][std::countr_zero(landPiece)];
								auto entryIt = entries.begin();
								auto startEntryIt = startEntries.begin();
								for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
									auto& rowK0 = rowP1[ik0];
									const U32 bbk0 = unrankFirstKing<false, P0C, P1C>(ik0, bbp0);
									const U32 bbk0_new = sourcePiece == bbk0 ? landPiece : bbk0;
									const U64 takeSplit = sourcePiece == bbk0 || isTakeMove ? unmoveSplit(Board::takeWinCards(bbk0_new, bbp1_new, cards.moveBoardsForward)) : childTakeSplits[ik0];
									for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++, entryIt++, startEntryIt++) {
										if (!*entryIt) { // no perm left that this move could help: resolved (e.g. win in 0), already has a move to an unresolved child, or lacks the cards
											if (*startEntryIt)
												newUnresolvedLandings |= landPiece; // not evaluated, may still help the start entry
											continue;
										}
										const U32 bbk1 = unrankSecondKing<false, P0C, P1C>(ik1, bbp1);
										if (landPiece == bbk1) // King takes are obviously resolved
											continue;

										// unmoveSplit permutes the card perm bits, so it commutes with | and with ~ under sideSplit.
										const U32 helped = unmoveJoin(~(takeSplit | (childTempleSplits[ik1] & templeMask)) & sideSplit);
										*entryIt &= ~helped;
										if (helped & *startEntryIt)
											newUnresolvedLandings |= landPiece;
									}
								}
							} else {
								const bool isTakeMove = landPiece & bbp1;
								const int landRankInv = std::popcount(bbp0_without_source & -landPiece);
								const std::atomic<U32>* childRow = childRows[childRowsStart[iSrc] + std::popcount(prefetchedLandings[iSrc] & (landPiece - 1))];
								std::array<U32, P0C * P1C> otherEntry; // unresolved bits of the child
								auto otherIt = otherEntry.begin();
								if (!isTakeMove) {
									for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
										auto& rowK0 = rowP1[ik0];
										const int ik0_inv = invertKingRank<P0C>(ik0) + (iSrc < ik0); // the source piece leaving from below shifts the inverted king up
										const U32 ik1_new = iSrc == ik0 ? landRankInv : ik0_inv - (landRankInv >= ik0_inv); // landing below the king shifts it down
										for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++, otherIt++) {
											const U32 ik0_new = invertKingRank<P1C>(ik1);
											*otherIt = childRow[ik0_new * P0C + ik1_new].load(std::memory_order_acquire);
										}
									}

								} else {
									if constexpr (P1C == 1) {
										std::unreachable();
									} else {
										const int iTaken = std::popcount(bbp1 & (landPiece - 1));
										for (int ik0 = 0; ik0 < static_cast<int>(rowP1.size()); ik0++) {
											auto& rowK0 = rowP1[ik0];
											const int ik0_inv = invertKingRank<P0C>(ik0) + (iSrc < ik0); // the source piece leaving from below shifts the inverted king up
											const U32 ik1_taken = iSrc == ik0 ? landRankInv : ik0_inv - (landRankInv >= ik0_inv); // landing below the king shifts it down
											for (int ik1 = 0; ik1 < static_cast<int>(rowK0.size()); ik1++, otherIt++) {
												if (ik1 == iTaken) { // King takes are obviously resolved
													*otherIt = 0;
													continue;
												}
												const U32 ik0_taken = invertKingRank<P1C>(ik1) - (ik1 < iTaken); // the taken piece shifts the king down if above it
												*otherIt = childRow[ik0_taken * P0C + ik1_taken].load(std::memory_order_acquire);
											}
										}
									}
								}

								const U32 sideCards = cards.moveBoardsForward.sideCards[pp][std::countr_zero(landPiece)];
								U32 startHelpers = 0;
								for (int i = 0; i < P0C * P1C; i++) {
									const U32 helpers = otherEntry[i] & sideCards;
									childHelpers[i] |= helpers;
									startHelpers |= helpers & startEntriesMoved[i];
								}
								if (startHelpers)
									newUnresolvedLandings |= landPiece;
							}
						}
						if constexpr (STEP > 1) {
							unresolvedUnion = 0;
							for (int i = 0; i < P0C * P1C; i++) {
								entries[i] &= ~unmoveCardEntry(childHelpers[i]);
								unresolvedUnion |= entries[i];
							}
						}
					}
				}
				// STEP 1 is the first pass over the landings, so it initializes them. 0 means none helped: every unresolved entry is lost below, so the block becomes resolved
				unresolvedLandings = STEP == 1 ? newUnresolvedLandings : unresolvedLandings & newUnresolvedLandings;

				// Unresolved card perms where every move leads to a resolved child, i.e. a win for the opponent.
				std::array<U32, P0C * P1C> newLostEntries;
				U32 lostUnion = 0;
				for (int i = 0; i < P0C * P1C; i++) {
					// Reload after the acquire loads of the children: a child that was seen resolved as a loss has
					// already marked its parents (this entry) before releasing, so those win bits must not count as lost.
					newLostEntries[i] = it[i].load(std::memory_order_acquire) & entries[i];
					lostUnion |= newLostEntries[i];
				}
				if (!lostUnion)
					continue;
				std::array<U64, P0C * P1C> lostSplits;
				for (int i = 0; i < P0C * P1C; i++)
					lostSplits[i] = unmoveSplit(newLostEntries[i]);

				{ // reverse movegen - all entries that can reach this entry are also marked as resolved.
					struct EntryToUpdate {
						std::atomic<U32>* pawnRow_new; // flat [ik0][ik1] king block of <P1C, P0C>
						[[no_unique_address]] std::conditional_t<P0C < TB_MEN / 2, std::atomic<U32>*, std::monostate> pawnRow_untaken; // flat king block of <P1C, P0C + 1>
						std::array<U32, P0C * P1C> newEntryBits; // per [ik0][ik1] of this entry, 0 = nothing to mark
						std::array<U8, P1C> ik0News; // per ik1
						int iUntaken;
					};
					std::array<EntryToUpdate, P1C * std::min(20, 25 - P0C - P1C)> entriesToUpdate;
					auto entriesToUpdateIt = entriesToUpdate.begin();
					const U32 usedCards = usedCardsOfEntry(lostUnion);
					U32 sourcePieces = bbp1;
					for (int iSrc = 0; iSrc < P1C; iSrc++) {
						const U32 sourcePiece = sourcePieces & -sourcePieces;
						int pp = std::countr_zero(sourcePieces);
						sourcePieces &= sourcePieces - 1;
						const U32 bbp1_without_source = bbp1 - sourcePiece;
						const int iUntaken = std::popcount(bbp0 & (sourcePiece - 1));
						U32 landPieces = cards.moveBoardsForward.forCards[usedCards][pp] & ~(bbp0 | bbp1);
						while (landPieces) {
							const U32 landPiece = landPieces & -landPieces;
							landPieces &= landPieces - 1;
							const U32 sideCards = cards.moveBoardsForward.sideCards[pp][std::countr_zero(landPiece)];
							if (!(lostUnion & sideCards))
								continue;
							const U32 bbp1_new = bbp1_without_source | landPiece;
							const int landRankInv = std::popcount(bbp1_without_source & -landPiece);

							const U64 sideSplit = cards.moveBoardsForward.unmoveSideCards[pp][std::countr_zero(landPiece)];

							auto& entry = *entriesToUpdateIt++;
							entry.iUntaken = iUntaken;
							for (int i = 0; i < P0C * P1C; i++)
								entry.newEntryBits[i] = unmoveJoin(lostSplits[i] & sideSplit);
							for (int ik1 = 0; ik1 < P1C; ik1++) {
								const int ik1_inv = invertKingRank<P1C>(ik1) + (iSrc < ik1); // the source piece leaving from below shifts the inverted king up
								entry.ik0News[ik1] = iSrc == ik1 ? landRankInv : ik1_inv - (landRankInv >= ik1_inv); // landing below the king shifts it down
							}

							const int ip0_new = rankFirstPieces<true, P1C, P0C>(bbp1_new);
							const int ip1_new = rankSecondPieces<true, P1C, P0C>(bbp0, bbp1_new); // TODO incremental?
							entry.pawnRow_new = &tb.template getRow<P1C, P0C>()[ip0_new][ip1_new][0][0];
							__builtin_prefetch(entry.pawnRow_new, 1, 0);
							if constexpr (P0C < TB_MEN / 2) {
								const U32 bbp0_untaken = bbp0 | sourcePiece;
								const int ip0_untaken = rankFirstPieces<true, P1C, P0C + 1>(bbp1_new);
								const int ip1_untaken = rankSecondPieces<true, P1C, P0C + 1>(bbp0_untaken, bbp1_new); // TODO incremental?
								entry.pawnRow_untaken = &tb.template getRow<P1C, P0C + 1>()[ip0_untaken][ip1_untaken][0][0];
								__builtin_prefetch(entry.pawnRow_untaken, 1, 0);
							}
						}
					}

					for (auto entryIt = entriesToUpdate.begin(); entryIt != entriesToUpdateIt; entryIt++) {
						const auto& [pawnRow_new, pawnRow_untaken, newEntryBitsArr, ik0News, iUntaken] = *entryIt;
						auto bitsIt = newEntryBitsArr.begin();
						for (int ik0 = 0; ik0 < P0C; ik0++) {
							const U32 ik1_new = invertKingRank<P0C>(ik0);
							for (int ik1 = 0; ik1 < P1C; ik1++, bitsIt++) {
								const U32 newEntryBits = *bitsIt;
								if (!newEntryBits)
									continue;
								const U32 ik0_new = ik0News[ik1];
								fetch_mask(pawnRow_new[ik0_new * P0C + ik1_new], newEntryBits, std::memory_order_relaxed);

								if constexpr (P0C < TB_MEN / 2) {
									const int ik1_untaken = ik1_new + (ik0 < iUntaken); // the untaken piece shifts the king up if above it
									fetch_mask(pawnRow_untaken[ik0_new * (P0C + 1) + ik1_untaken], newEntryBits, std::memory_order_relaxed);
								}
							}
						}
					}
				}

				for (int i = 0; i < P0C * P1C; i++) {
					// Only after the reverse movegen, so a thread that sees these bits set also sees the parents marked.
					if (newLostEntries[i])
						it[i].fetch_and(~newLostEntries[i], std::memory_order_release);
				}
				updated = true;
			}
		}
	}
	rowStartChunk += P0_CHUNKS;
}

// Writes win in 0 and win in 1 into every entry of the row. This is also the first touch of the row.
template <U16 P0C, U16 P1C>
void prefillRow(const CardsInfo& cards, auto& tb, U64& chunk, U64& rowStartChunk, std::atomic<U64>& chunkCounter) {
	auto& row = tb.template getRow<P0C, P1C>();
	constexpr U32 OUTER_SIZE = PAWNTABLE_P0<P0C, P1C>.size();
	constexpr U32 INNER_SIZE = PAWNTABLE_P1<P0C, P1C>.size();

	for (; chunk < rowStartChunk + OUTER_SIZE; chunk = chunkCounter++) {
		const int ip0 = static_cast<int>(chunk - rowStartChunk);
		const U32 bbp0 = unrankFirstPieces<false, P0C, P1C>(ip0);

		// Per square of p1's king: the cards with which p0 takes it. p1's king on its temple is win in 0, which clears every bit.
		std::array<U32, 25> takeCards{};
		for (int i = 0; i < 5; i++) {
			U32 attacked = 0;
			for (U32 pieces = bbp0; pieces; pieces &= pieces - 1)
				attacked |= cards.moveBoardsForward.moveBoards[i][std::countr_zero(pieces)];
			for (; attacked; attacked &= attacked - 1)
				takeCards[std::countr_zero(attacked)] |= P_HAS_CARD_IN_MASK<0>[i];
		}
		takeCards[PTEMPLE[1]] = CARD_PERMS_MASK;

		const bool templeFree = !(bbp0 & (1U << PTEMPLE[0]));
		std::array<U32, P0C> k0Masks;
		U32 kings0 = bbp0;
		for (int ik0 = 0; ik0 < P0C; ik0++, kings0 &= kings0 - 1) {
			const U32 bbk0 = kings0 & -kings0;
			k0Masks[ik0] = Board::isPlayerTempleEnded<0>(bbk0) ? 0 : CARD_PERMS_MASK & ~(templeFree ? Board::templeKingCards<0>(bbk0, cards.moveBoardsReverse) : 0);
		}

		auto* it = &row[ip0][0][0][0];
		for (int ip1 = 0; ip1 < static_cast<int>(INNER_SIZE); ip1++) {
			std::array<U32, P1C> k1Masks;
			U32 kings1 = unrankSecondPieces<false, P0C, P1C>(ip1, bbp0);
			for (int ik1 = 0; ik1 < P1C; ik1++, kings1 &= kings1 - 1)
				k1Masks[ik1] = ~takeCards[std::countr_zero(kings1)];
			for (int ik0 = 0; ik0 < P0C; ik0++)
				for (int ik1 = 0; ik1 < P1C; ik1++)
					(it++)->store(k0Masks[ik0] & k1Masks[ik1], std::memory_order_relaxed);
		}
	}
	rowStartChunk += OUTER_SIZE;
}

// STEP 0 prefills every row before anything can reverse mark it: the rows of p1c == 1 of the group itself, which nothing
// else marks, and the rows its reverse movegen untakes into, which belong to the next piece count sum.
template <U16 TB_MEN, U16 P0C, U16 P1C>
void prefillRowsOf(const CardsInfo& cards, auto& tb, U64& chunk, U64& rowStartChunk, std::atomic<U64>& chunkCounter) {
	if constexpr (P1C == 1)
		prefillRow<P0C, P1C>(cards, tb, chunk, rowStartChunk, chunkCounter);
	if constexpr (P0C < TB_MEN / 2)
		prefillRow<P1C, P0C + 1>(cards, tb, chunk, rowStartChunk, chunkCounter);
}

template <U16 TB_MEN, int STEP, typename Storage, typename Landings>
void singleDepthPass(const CardsInfo& cards, Storage& tb, Landings& landings, std::size_t group, std::atomic<U64>& chunkCounter, bool& updated) {
	U64 chunk = chunkCounter++;
	U64 rowStartChunk = 0;

	[&]<std::size_t... I>(std::index_sequence<I...>) {
		([&]<U16 P0C, U16 P1C> {
			if (I != group)
				return;
			if constexpr (STEP == 0) {
				prefillRowsOf<TB_MEN, P0C, P1C>(cards, tb, chunk, rowStartChunk, chunkCounter);
				if constexpr (P0C != P1C)
					prefillRowsOf<TB_MEN, P1C, P0C>(cards, tb, chunk, rowStartChunk, chunkCounter);
				updated = true;
			} else {
				processRow<TB_MEN, P0C, P1C, STEP>(cards, tb, landings, chunk, rowStartChunk, chunkCounter, updated);
				if constexpr (P0C != P1C)
					processRow<TB_MEN, P1C, P0C, STEP>(cards, tb, landings, chunk, rowStartChunk, chunkCounter, updated);
			}
		}.template operator()<PIECE_COUNT_ORDER<TB_MEN>[I].p0c, PIECE_COUNT_ORDER<TB_MEN>[I].p1c>(), ...);
	}(std::make_index_sequence<PIECE_COUNT_ORDER<TB_MEN>.size()>{});
}

template <U16 TB_MEN>
constexpr std::size_t groupOfRow(U16 p0c, U16 p1c) {
	for (std::size_t i = 0; i < PIECE_COUNT_ORDER<TB_MEN>.size(); i++)
		if (PIECE_COUNT_ORDER<TB_MEN>[i].p0c == std::min(p0c, p1c) && PIECE_COUNT_ORDER<TB_MEN>[i].p1c == std::max(p0c, p1c))
			return i;
	throw "row not in any group";
}

// Rows of groups after lastGroup may not be prefilled yet, they count as fully unresolved.
template <U16 TB_MEN, typename Storage>
U64 countUnresolved(const Storage& tb, std::size_t lastGroup) {
	// Only called while the workers are idle, so the entries are read as plain U64 words.
	constexpr U64 CHUNK_WORDS = 1 << 16;
	std::vector<std::span<const U64>> chunks;
	U64 tailCount = 0;
	tb.forEachRow([&]<U16 P0C, U16 P1C> {
		const auto& row = tb.template getRow<P0C, P1C>();
		const U64 entries = row.size() * sizeof(row[0]) / sizeof(U32);
		if (groupOfRow<TB_MEN>(P0C, P1C) > lastGroup) {
			tailCount += entries * 30;
			return;
		}
		const U64* words = reinterpret_cast<const U64*>(row.data());
		for (U64 i = 0; i < entries / 2; i += CHUNK_WORDS)
			chunks.emplace_back(words + i, std::min(CHUNK_WORDS, entries / 2 - i));
		if (entries % 2)
			tailCount += std::popcount(reinterpret_cast<const U32*>(words)[entries - 1]);
	});

	std::atomic<U64> nextChunk = 0;
	std::atomic<U64> count = tailCount;
	{
		std::vector<std::jthread> threads;
		for (unsigned i = 0; i < std::max(1u, std::thread::hardware_concurrency()); i++)
			threads.emplace_back([&] {
				U64 local = 0;
				for (U64 c; (c = nextChunk++) < chunks.size();)
					for (const U64 word : chunks[c])
						local += std::popcount(word);
				count += local;
			});
	}
	return count;
}

template <U16 TB_MEN>
consteval U64 countTotal() {
	U64 total = 0;
	TableBase<TB_MEN>::Storage::forEachRow([&]<U16 P0C, U16 P1C> {
		total += 30ULL * PAWNTABLE_P0<P0C, P1C>.size() * PAWNTABLE_P1<P0C, P1C>.size() * P0C * P1C;
	});
	return total;
}

template <U16 TB_MEN, typename Storage, typename Landings>
void singleThread(const CardsInfo& cards, Storage& tb, Landings& landings, std::atomic<U64>& chunkCounter, ThreadObj& comm) {
	while (true) {
		comm.sync.slaveNotifyWait();
		if (comm.exit)
			break;

		bool updated = false;
		if (comm.iteration == 0)
			singleDepthPass<TB_MEN, 0>(cards, tb, landings, comm.group, chunkCounter, updated);
		else if (comm.iteration == 1)
			singleDepthPass<TB_MEN, 1>(cards, tb, landings, comm.group, chunkCounter, updated);
		else
			singleDepthPass<TB_MEN, 2>(cards, tb, landings, comm.group, chunkCounter, updated);
		if (updated)
			comm.updated.store(true, std::memory_order_relaxed);
	}
}

template <U16 TB_MEN, typename Storage, typename Landings>
void runTableBaseBuild(const CardsInfo& cards, Storage& tb, Landings& landings, U64 stopAtIteration) {
	const auto startTime = std::chrono::steady_clock::now();
	// 30 card perms. 47 perms with kings on their temple. times all combinations of zero to 2 pawns on each side
	// constexpr U64 EXPECTED_WIN_IN_ZERO = 30 * 47 * (1 + 23 + 23*22/2 + 23 * (1 + 22 + 22*21/2) + 23*22/2 * (1 + 21 + 21*20/2));
	constexpr U64 EXPECTED_RESOLVED_STATES = TB_MEN == 6 ? 1166580494ULL : TB_MEN == 8 ? 50958224689ULL : 1038519662776ULL;

	std::atomic<U64> chunkCounter;
	ThreadObj comm;
	int numThreads = std::clamp<int>(static_cast<int>(std::thread::hardware_concurrency()), 1, 1024);
	std::vector<std::thread> threads(numThreads);
	for (int i = 0; i < numThreads; i++)
		threads[i] = std::thread(singleThread<TB_MEN, Storage, Landings>, std::cref(cards), std::ref(tb), std::ref(landings), std::ref(chunkCounter), std::ref(comm));
	comm.sync.masterWait(numThreads);

	std::chrono::duration<double> countingTime{};
	U64 resolvedStates = 0;
	U64 newResolvedStates = 1;
	constexpr U64 total = countTotal<TB_MEN>();
	// Takes only lead to groups earlier in PIECE_COUNT_ORDER, so each group is solved completely before the next one.
	for (std::size_t group = 0; group < PIECE_COUNT_ORDER<TB_MEN>.size(); group++) {
		comm.group = group;
		comm.iteration = 0;
		comm.updated = true;
		std::cout << std::format("{}v{}", PIECE_COUNT_ORDER<TB_MEN>[group].p0c, PIECE_COUNT_ORDER<TB_MEN>[group].p1c);
		if constexpr (VERBOSE)
			std::cout << ":\n";
		else
			std::cout << std::flush;
		std::chrono::duration<double> groupTime{};
		while (comm.updated && comm.iteration <= stopAtIteration) {
			chunkCounter = 0;
			comm.updated = false;
			const auto iterationStart = std::chrono::steady_clock::now();
			comm.sync.masterNotify(numThreads);
			comm.sync.masterWait(numThreads);
			const auto countingStart = std::chrono::steady_clock::now();
			const std::chrono::duration<double> iterationTime = countingStart - iterationStart;
			groupTime += iterationTime;

			if constexpr (VERBOSE) {
				const U64 count = total - countUnresolved<TB_MEN>(tb, group);
				newResolvedStates = count - resolvedStates;
				resolvedStates = count;
				if (iterationTime.count() > .03 || newResolvedStates == 0)
					std::cout << std::format("it {:3}: {:12} ({:.4f}%) in {:.2f}s\n", comm.iteration, newResolvedStates, 100.0 * resolvedStates / total, iterationTime.count());
			} else
				std::cout << "." << std::flush;

			countingTime += std::chrono::steady_clock::now() - countingStart;
			comm.iteration++;
		}
		if constexpr (!VERBOSE)
			std::cout << std::format(" {:.2f}s\n", groupTime.count());
	}

	const std::chrono::duration<double> totalTime = std::chrono::steady_clock::now() - startTime - countingTime;

	if constexpr (!VERBOSE) {
		const auto countingStart = std::chrono::steady_clock::now();
		resolvedStates = total - countUnresolved<TB_MEN>(tb, PIECE_COUNT_ORDER<TB_MEN>.size() - 1);
		countingTime += std::chrono::steady_clock::now() - countingStart;
	}
	if constexpr (VERBOSE)
		std::cout << "\n";
	std::cout << std::format("total {}-men: {} states ({:.4f}%) in {:.2f}s (+{:.2f}s counting)\n\n", TB_MEN, resolvedStates, 100.0 * resolvedStates / total, totalTime.count(), countingTime.count());
	if (resolvedStates != EXPECTED_RESOLVED_STATES) {
		std::cerr << "ERROR: WRONG NUMBER OF RESOLVED BOARDS (got " << resolvedStates << ", expected " << EXPECTED_RESOLVED_STATES << ")\n";
		throw std::runtime_error("wrong number of boards");
	}

	comm.exit = true;
	comm.sync.masterNotify(numThreads);
	for (auto& thread : threads)
		thread.join();
}

export template <U16 TB_MEN>
struct TableBase {
	using CardsEntry = std::atomic<U32>;

	template <U16 P0C, U16 P1C>
	using KingsRow = std::array<CardsEntry, P1C>;

	template <U16 P0C, U16 P1C>
	using KingsBlock = std::array<KingsRow<P0C, P1C>, P0C>;

	template <U16 P0C, U16 P1C>
	using PawnRow = std::array<KingsBlock<P0C, P1C>, PAWNTABLE_P1<P0C, P1C>.size()>;

	template <U16 P0C, U16 P1C>
	struct alignas(64) Row : std::array<PawnRow<P0C, P1C>, PAWNTABLE_P0<P0C, P1C>.size()> {};

	// Unresolved landings of each block, in processRow's iteration order ([ip0_new][ipInner] of the mirrored row) so it is streamed. 0 = block resolved.
	template <U16 P0C, U16 P1C>
	struct alignas(64) LandingsRow : std::array<std::array<U32, PAWNTABLE_P1<P1C, P0C>.size()>, PAWNTABLE_P0<P1C, P0C>.size()> {};

	// One tuple of ROW<P0C, P1C> per piece count, in huge pages. Left uninitialized.
	template <template <U16, U16> typename ROW>
	struct RowTuple {
		template <std::size_t... I>
		static auto _Rows(std::index_sequence<I...>) -> std::tuple<ROW<ROW_ORDER<TB_MEN>[I].p0c, ROW_ORDER<TB_MEN>[I].p1c>...>;
		union { // In a union so the tuple's value-initialization doesn't zero the whole table.
			decltype(_Rows(std::make_index_sequence<ROW_ORDER<TB_MEN>.size()>{})) rows;
		};

		RowTuple() {}

		// madvise has to happen before the first touch, which is the prefill or the first pass of the build.
		static void* operator new(std::size_t size) {
			constexpr std::size_t HUGE_PAGE = 2 << 20;
			const std::size_t rounded = (size + HUGE_PAGE - 1) / HUGE_PAGE * HUGE_PAGE;
			void* p = std::aligned_alloc(HUGE_PAGE, rounded);
			if (!p)
				throw std::bad_alloc();
			if (madvise(p, rounded, MADV_HUGEPAGE))
				std::cerr << std::format("madvise(MADV_HUGEPAGE) failed: {}\n", std::strerror(errno));
			return p;
		}
		static void operator delete(void* p) { std::free(p); }

		static constexpr void forEachRow(auto&& f) {
			[&]<std::size_t... I>(std::index_sequence<I...>) {
				(f.template operator()<ROW_ORDER<TB_MEN>[I].p0c, ROW_ORDER<TB_MEN>[I].p1c>(), ...);
			}(std::make_index_sequence<ROW_ORDER<TB_MEN>.size()>{});
		}

		template <U16 P0C, U16 P1C>
		auto& getRow(this auto& self) {
			return std::get<rowIndex<TB_MEN>(P0C, P1C)>(self.rows);
		}
	};

	using Storage = RowTuple<Row>;
	using Landings = RowTuple<LandingsRow>; // only needed during the build

	explicit TableBase(const CardsInfo& cards, U64 stopAtIteration = std::numeric_limits<U64>::max()) {
		tb = std::make_unique<Storage>();
		auto landings = std::make_unique<Landings>(); // STEP 1 writes every entry

		std::cout << std::format("allocated {:.1f}GB + {:.1f}GB\n", sizeof(Storage) / 1e9, sizeof(Landings) / 1e9);

		runTableBaseBuild<TB_MEN>(cards, *tb, *landings, stopAtIteration);
	}

	std::unique_ptr<Storage> tb;
};
