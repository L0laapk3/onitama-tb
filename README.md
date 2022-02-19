# onitama-tb

Onitama-tb is a tablebase generator for the onitama board game. Onitama is a simple board game with 10 pieces where the goal is to take the opponent king or reach the opponent temple with your king.
At the start of the game, 5 out of 16 cards are chosen to play the game with, which results in 2348 possible card sets (when excluding symmetry) with which a game can be played.

This repo is a rewrite of https://github.com/L0laapk3/Onitama-bot, which itself is a rewrite of a previous bot.
When I started adding tablebases to my bot a year ago, generating all the 4 men tablebase boards (~8 million) took upwards of a minute and 16GB ram.
Since then, this has been optimized to the point where a 6 men TB (~1E9 entries) can be generated in a couple seconds, 8 men (~47E9 entries) in a couple minutes, and 10 men (~800E9 entries so all possible forced wins) in a couple hours with 32/64GB of ram.
This was achieved by experimenting with many different techniques, the most noteworthy of which were pushed to their own branch (many older ideas also ended up in branches in the Onitama-bot repo).

With 10 men TB generation now being in reach, the goal is to squeeze out more performance to get 10 men TB to a timeframe such that it can reasonably be used before the start of a game.
An alternative goal is to generate (partial) TB ahead of time, compress and store the terrabytes of data, and use them.

A brief overview of the most noteworthy optimisations:
* An indexing function is used that converts each board to an unique index and back, this way the whole tablebase can be stored as a giant array.
Since the TB only stores player 1 boards, inverted indexing functions are also present.
  - A ton of time went into optimising this, This function makes heavy use of pdep/pext intrinsic instructions (use case optimized alternatives are also implemented for zen/zen2), look-up tables, etc.
* The vast majority of boards are close to forced wins, and this fact is heavily exploited:
  - When generating the TB, iteration is done over the not yet resolved boards (Credit to Maxbennedich for this trick) and the vast majority of boards are resolved in the first couple iterations.
    The first iterations are also specified to not even perform look-ups in the TB, and otherwise a win in 1 check is performed before look-up. This massively helps alleviate the memory bottleneck.
  - The index function excludes all win in 1's (~65% of total boards).
      - This reduces the 10 men TB requirements from 240GB to 96GB, further reduced 64GB using realtime lz4 compression.
      - This shifts the bottleneck balance from memory to cpu, and results in significant runtime improvements on powerful CPU's (20% reduction on 12850H 8C/16T 8men).
* If only draw info and not win/loss info is desired, this can be generated with half the memory, so 10 men can be done in 32GB (and ~5% less runtime).
  - There are not a lot of draws (~0.01% - ~0.1%), so this massively shrinks down the required storage space for ahead-of-time, making ahead of time generation for all 2348 card sets realistic.
    When all draws are known, a lot of information about forced wins and losses can be implicitely extracted, but experiments are still needed if this is sufficient for perfect gameplay.
* The order of a tablebase generation has been meta-optimized such that previous results in an iteration are re-used as much as possible.
  On the testing card set, which has win in 92 plies, all the forced wins can be found in just 18 iterations.
* Many, many, many other optimizations have been tried, and a lot of them made it into the project that I forgot about.
