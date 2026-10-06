# Notes for agents

Onitama endgame tablebase generator. C++26 modules, Bazel, Clang + libc++. Performance matters: code in the
per-entry loops is the hot path, so prefer constexpr lookup tables over runtime work there.

## Build / run

```bash
bazel run //tb   # optimized; this is how the generator is normally run
bazel run //tb 8 # Larger tablebase size for benchmarking
bazel run //tb --config=symbols
bazel run //tb --config=dbg
```
The real tablebase is 10 large, but this is too large for reasonable testing and does not fit in host computer memory.

`tb/main.cpp` picks the card set and `TB_MEN`. The build itself has self-checks: it prints counts per depth and
throws if known totals (win in 0, win in 1, total resolved) don't match. Treat those as the test suite, but
expected constants can go stale when the counting changes, so sanity check them before "fixing" code to match.

## Branches

- `master`: the original, mature generator (`src/`, CMake). Reference implementation.
- `hytak_alg`: the rewrite in `tb/` (Bazel + modules).

## The hytak_alg approach

Each table entry is a `U32` bitmask over all 30 card permutations of one piece configuration, so a whole
position-for-all-cards is processed in one go. A set bit means "unresolved" for that card permutation, so the table is initialized to `CARD_PERMS_MASK`
in every entry and resolving clears bits.
Pieces are indexed per row (one row per (p0 count, p1 count), kings included in the counts) as
`row[ip0][ip1][ik0][ik1]`:

- `ip0`: placement of the first player's pieces on 25 squares (colex order, `PIECE_PLACEMENTS`).
- `ip1`: placement of the second player's pieces on the remaining free squares (deposited with `pdep`).
- `ik0`/`ik1`: which of each player's pieces is the king.

Step 1 (depth 2) resolves win-in-0 / win-in-1 without table lookups; later steps use lookups into the table.
The trade-off vs master: because all card permutations share an index, master's trick of dropping
win-in-1 positions from the index space (which depends on the cards) is not available.

### Inversion (`invert` template parameter)

Index functions with `invert = true` decode/encode the same index as seen by the other player: board rotated
180 degrees with colours swapped. Rotation is a 25-bit bit reversal (`PTEMPLE` = {22, 2} maps onto itself).

Pitfalls learned the hard way:

- Reversing a colex *index* (`size - 1 - i`) is NOT the same as rotating the board. Use bit-reversed tables.
- Bit reversal commutes with `pdep`: reversing a compact placement over its own N free squares and then
  depositing it into the (already rotated) free-square mask equals rotating the deposited board. So inverted
  decoding should be a lookup into a precomputed reversed table (`PAWNTABLE_*_INV`) plus the usual `pdep`,
  with no runtime reversal.
- With `invert = true`, the "first" pieces in the index belong to player 1 and the "second" to player 0;
  make sure piece counts (`p0c`/`p1c`) and the bitboard passed as the `pdep` mask match the right player.

## How master does it (for reference)

`git show master:src/Index.hpp` (also `TableBase.hpp`, `Board.h`, `Card.hpp`).

- Index = (piece-count row, king pair) and a pawn index. Kings are a separate table over
  all king-square pairs, excluding pairs where a king already stands on its winning temple (win in 0),
  `KINGSMULT = 23*24+1`.
- Index space is reduced per card set: P0 pawns may not sit on squares that attack P1's king (that would be a
  win in 1) and if P0's king threatens a temple win, a pawn is forced onto the temple. These are encoded as a
  compaction mask (`pext`/`pdep` around kings and forbidden squares).
- Pawn placements are lookup tables by pawn count; inverted variants (`TABLE_*_INV`, `TABLES_BBKINGS[1]`)
  are stored bit-reversed and shifted down to the actual number of free squares at decode time. Encoding the
  inverted board scans with `lzcnt` instead of `tzcnt`. No runtime bit reversal.
- `boardToIndex<invert>` / `indexToBoard<invert>` are documented as equal to doing the non-inverted
  operation on `board.invert()`; that identity is the thing to test when touching indexing.

## Debugging tips

- When a count is off, reproduce the decoding in a small standalone script (Python works; it's slow but the
  6-men table is enumerable in a few minutes) and check validity of every decoded board: pieces within
  25 bits, no overlap, exactly one king per side and the king is among its pieces, and that the inverted
  decode equals the rotated normal decode.
- Multithreaded passes use relaxed atomics; small run-to-run variance in some counters can come from races
  rather than indexing bugs.
- When I run a benchmark, a few more seconds of variation is expected compared to the users benchmark due to the nature of the harnass.