# Performance plan for the hytak_alg generator

Goal: close the gap to [LHolten/onitama-solver](https://github.com/LHolten/onitama-solver), which reportedly runs
8 men in about 12 s on the same machine (i9-13900KF, 32 threads, WSL2). Read `AGENTS.md` first.

Work through the open tasks one at a time. Each task is meant to be one change, benchmarked and verified on its own.

## How to benchmark and verify

```bash
bazel build //tb && bazel-bin/tb/tb 6   # quick correctness check, must print 1166580494 states without throwing
bazel-bin/tb/tb 8                       # benchmark, compare the "total 8-men: ... in X seconds" line and per-iteration times
```

- The resolved total (`EXPECTED_RESOLVED_STATES` in `runTableBaseBuild`) is the test. Per-iteration counts may shift
  slightly between runs or orderings (in-place updates race between threads), the final total must not.
- Expect about 1-3 s of run-to-run noise on 8 men.
- WSL2 has no hardware perf counters (`cycles`, `cache-misses` are `<not supported>`). Software sampling works:
  ```bash
  bazel build //tb --config=symbols && cp -f bazel-bin/tb/tb /tmp/tb_sym
  perf record -e cpu-clock -F 499 -o /tmp/perf.data /tmp/tb_sym 8
  perf report -i /tmp/perf.data --no-children --sort srcline --stdio   # slow (several minutes)
  ```
  Everything is inlined into `singleDepthPass<..., STEP>`, so `--sort sym` only splits STEP 1 from STEP 2.
- Temporary work counters (thread-local, summed per pass, printed in the iteration line) were very useful for
  reasoning about where time goes: blocks forward-processed, child lookups, blocks with new losses, parent moves,
  parent entries touched. Add them while working, remove before committing.

## History

| State | 8 men total |
| --- | --- |
| Baseline (commit `2e0dbb6`) | 62.9 s |
| After done tasks 1-4 (commit `6055b2d`) | 38.6 s |

Measured counters at baseline, iteration 2: about 1.18e8 blocks forward-processed (nearly all blocks), 2.2e9 child
block lookups, 1.7e9 parent moves, 4.6e9 parent entries with new bits. After iteration 1, almost every block still
has at least one unresolved bit, so "skip resolved blocks" alone only helps in the tail.

## Done

These are implemented together in commit `6055b2d` (`6055b2de724e560f45ca12a8dc66360c8cef6fb7`), which is on top of
`2e0dbb6` but not on any branch. It is kept alive by the ref `refs/experiments/perf-tasks-1-4`:

```bash
git show 6055b2d                       # the full diff for tasks 1-4
git diff 2e0dbb6 6055b2d -- tb/tablebase.cppm
git cherry-pick 6055b2d                # or apply it wholesale
```

To redo a single task in a guided way, use that diff as the reference implementation; each task below names the
functions/variables it touched.

### 1. Iterate with the non-moving side's pieces in the outer loop (done)

Blocks are stored as `row[mover pieces][other pieces][ik0][ik1]` (the current row is decoded with `invert = true`).
A non-capture child is `MIRROR_ROW[other pieces (unchanged)][mover pieces after move]`. The old loop walked the inner
(other pieces) index, so each consecutive block's children lived in a different ~380 KB `MIRROR_ROW[ip0_new]` slab and
almost every child lookup missed cache.

`processRow` now loops over `ip0_new` (placement of `bbp0` on 25 squares, `PAWNTABLE_P0<MIRROR_ROW>`) outside and the
mover's placement (`PAWNTABLE_P1<MIRROR_ROW>`, deposited on `~bbp0`) inside, computing the current block's index with
the inverted `rankFirstPieces` / `rankSecondPieces`. All non-take children of one outer iteration share one slab
that fits in L2. `ip0s_taken` is hoisted to the outer loop. Constants live in `RowIteration<TB_MEN, ROW>`.

Effect: iterations 2-4 went from about 10.0 / 8.6 / 7.1 s to 8.2 / 6.4 / 5.2 s. Downside: the current block is now
read in random order (one miss per block), which made full scans slower until task 4.

### 2. Relaxed load before `fetch_and` (done)

Parent marks and the STEP 1 win-in-1 clear only issue the RMW if `load(relaxed) & bits` is non-zero.
Effect was within noise (about 1 s), kept because it is free and avoids dirtying lines.

### 3. Sparse reverse movegen (done)

The reverse movegen used to loop over all `p0c * p1c` king placements for each parent move and compute
`unmoveCardEntry(newLost[i] & sideCards[pp][land])` plus ranks for every one, but only about 3 of 16 (iteration 2)
and about 1 of 16 (later) had new losses. Now:
- `lostKings` bitmask (bit `ik0 * p1c + ik1`) and `lostUnion` (OR of all new losses) are built per block.
- A parent move is skipped entirely (before ranking) if `!(lostUnion & sideCards)`.
- Only set bits of `lostKings` are visited, and `ik0`/`ik1`/king squares are derived from the bit index.

Effect: 59 s to 49 s when applied on top of tasks 1 and 2. Applied alone on the baseline it gives about 63 s to
55 s: with the original loop order the reverse pass is more limited by cache misses, so removing compute there helps
less. Gains of these tasks are not additive; measure each one against the state it is applied to.

### 4. Active-block bitmap (done)

`makeActiveBlocks<TB_MEN>()` allocates one bit per block in iteration order (per row, per outer position,
`ACTIVE_WORDS` words, padding bits zero; offsets from `activeOffset<TB_MEN, ROW>()`). STEP 2 skips blocks whose bit
is clear without touching the table; a block's bit is cleared when its scan finds every entry resolved. Each outer
position is owned by one thread per pass, so plain `U64` words are safe. This mirrors `layouts.retain` in the
friend's solver.

Effect: tail passes went from 0.42 s to 0.05 s, total 49 s to 39.6 s.

## Open tasks (suggested order)

Current profile split: STEP 1 (iteration 1) about 23% of samples / 9.2 s; STEP 2 the rest. Counting
(`countUnresolved`) is excluded from the reported total.

### 5. Replace STEP 1 movegen with a cheap init pass + a normal lookup pass

Problem: iteration 1 costs about 9.2 s. For every child of every block and every king placement it builds a `Board`
and calls `getWinInOneCards<0>` (5 cards each), about 19 moves x 16 kings x 5 cards per block. This is pure compute.

Friend's approach: `mark_ez_win` marks win-in-1 states up front (cheap, no movegen), then the first real iteration is
an ordinary lookup iteration.

Plan:
- Iteration 1 becomes an init-only pass: per entry, win in 0 (`isTempleEnded`) stores 0, otherwise clear
  `getWinInOneCards<1>` bits. No forward or reverse movegen. Clear the active bit of fully resolved blocks.
- Iteration 2 onwards is the existing STEP 2. Its first run finds exactly what STEP 1's forward movegen found
  (positions where every move leads to an opponent win in 1), via lookups.
- Delete the `STEP == 1` branches in the forward movegen.

Watch out:
- STEP 1 currently has to initialize every entry of a block before jumping (see `blockHasUnresolved`); the init pass
  must touch every entry, and must finish completely (barrier) before any lookup pass starts.
- Win in 0 entries must not be treated as "lost" by the reverse pass (they are resolved with 0, and forward movegen
  skips king-take children explicitly).
- Iteration numbering in output shifts by one; the final total must stay the same.
- The init pass streams the table sequentially, so iterate it in storage order, not the task 1 order.

Expected: iteration 1 drops to well under 1 s, the next pass costs roughly what iteration 2 costs today (about 6 s).
Net saving a few seconds. If the lookup pass ends up slower than the current STEP 1, an alternative is to keep STEP 1
but make the win-in-1 test cheaper: per landing, compute take-win cards once per `ik0` (target king) and temple-win
cards once per `ik1`, and OR them, instead of 16 x 5 card checks (precomputed `[kingSq][attackerSq] -> card mask`
tables help).

### 6. Only re-check blocks whose children changed

Problem: in STEP 2 every not-fully-resolved block re-runs the full forward movegen (about 19 child block loads x 16
entries) every pass, even if none of its children changed. In iteration 2 that is about 2.2e9 child lookups; in the
draw-heavy tail about 2e7 per pass for blocks that never change.

Idea: a block B can only gain new losses if one of its children got newly resolved since B was last checked. The
chain is: block L is found lost, the reverse pass marks its parents P as won, and the blocks that must be re-checked
are the parents of those P (their child P changed). So the dirty set for the next pass is "parents of entries that
just became won", one reverse step beyond what the reverse pass touches today.

Plan:
- Keep a second bitmap ("dirty"), one bit (or byte) per block, initially all set.
- When the reverse pass actually clears bits in a parent P (the `fetch_and` that changes something, both `pawnRow_new`
  and `pawnRow_untaken`), mark P's parents dirty. Do this at block granularity only: un-move each of P's to-move
  pieces (ignoring kings and card masks, which may over-mark but never under-mark) and set the dirty bit of each
  resulting block.
- Dirty bits are set by threads that don't own those words: use `fetch_or` on the word, or one byte per block with
  plain relaxed stores, to avoid lost updates.
- STEP 2 only forward-processes blocks that are active AND dirty, and clears the dirty bit before processing (so a
  mark arriving during processing is not lost).
- First measure whether the extra block-level reverse step is cheaper than the forward re-checks it saves: count, per
  pass, how many active blocks would be clean.
- Mapping a parent block (row, ip0, ip1 in storage index) to its bit in iteration order needs the outer/inner
  iteration indices (`ip0_new` = rank of the non-mover pieces over 25 squares, inner = rank of the mover's pieces on
  `~bbp0`). Consider switching the bitmaps to storage order instead, indexed by the block's flat storage index, so
  both the iterating thread and the marking thread compute it trivially.

Verify with the 6-men check: missing a dirty mark shows up as too few resolved states.

### 7. Prefetch the current block

Since task 1, the current block (`row[rank(bbp1)][rank(bbp0, bbp1)]`) is read in random order, one cache miss per
block before any work can start (the early "is the block resolved" scan stalled on it in profiles). Compute the
index of the next active inner position one step ahead and `__builtin_prefetch` it. Small, self-contained change;
measure on iterations 2-6.

Also: the existing `__builtin_prefetch` calls for child/parent rows are immediately followed by the loads they
prefetch, so they do nothing. Either hoist them (compute all child/parent addresses for a block first, prefetch all,
then do the king loops) or remove them.

### 8. Hoist card masks per direction

`cards.moveBoardsReverse.sideCards[pp][land]` depends only on the move direction (within board bounds). The friend
precomputes, per direction, the expanded/unmoved card bits of all entries in a block once, then reuses them for every
piece moving in that direction. In the reverse pass, `unmoveCardEntry(newLost[i] & sideCards)` could be cached per
(direction, i) per block. Lower priority now that task 3 already made the loop sparse; profile first.

### 9. (Larger) Joint two-player index for streaming locality

The friend ranks a layout jointly over both players' pieces (multinomial colex, `ranking` in
`src/onitama_simd/iter.rs`). Consecutive layouts then have children and parents at nearby indices for every move,
so all lookups become a few sequential streams that the hardware prefetcher handles and lines are reused across
consecutive layouts. The split `[ip0][ip1]` index cannot give that for both children and parents at the same time
(task 1 makes children L2-local, parents are about 30 streams per outer position, the current block is random).

This is a redesign of `index.cppm` and the table layout, with all the inversion pitfalls in `AGENTS.md`. Only attempt
after tasks 5-8, and only if profiling still shows the per-block lookups (not compute) dominating.

### 10. Per-material scheduling (investigate)

The friend solves one material pair (and its colour-swap) to convergence before moving to the next, smallest first.
Captures then always look up final tables, and the "untaken" parent marking happens in one pass after convergence
(`go_up`). This keeps the working set to one or two tables. It is unclear how much this matters here, since the 4v4
row is most of the 6.8 GB anyway; measure with counters (per-row time per iteration) before trying.
