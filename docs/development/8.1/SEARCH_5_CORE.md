# Search rewrite, block 5: the core of the search

Part of the 8.1 plan in [`DEVELOPMENT_8.1.md`](../../../DEVELOPMENT_8.1.md). This block covers the parts of the search
that the first four blocks do not touch: quiescence search, the use of the transposition table (probe, cutoffs,
store), iterative deepening with aspiration windows, and the threads of the parallel search.

## 5.1 Scope

- **Quiescence**: stand-pat, the choice of captures and checks, the pruning of losing captures, and the value
  returned on a cutoff.
- **Transposition table use**: when a stored entry ends the search of a node, how its value and depth are trusted,
  and what is stored on exit. The table itself (layout, replacement, prefetch) is already our own and stays.
- **Iterative deepening and aspiration windows**: the window around the previous score, how it widens after a fail,
  and the root move order between iterations.
- **Threads**: the shared table, the helper threads and the vote that picks the move. Scaling with 4 and 8 threads
  matters for the 4CPU lists and is measured on its own.

## 5.2 Method

The same cycle as the other blocks: a written design, code behind an option (off, the tree is identical), a
non-regression SPRT, then the old code is removed. The four experts of the network enter where the confidence in
the static evaluation matters: the stand-pat margin in quiescence and the trust given to stored values.

Status: planned, after block 4.
