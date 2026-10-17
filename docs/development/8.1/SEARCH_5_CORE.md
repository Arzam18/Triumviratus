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

The four steps of block 1 ([section 1.1](SEARCH_1_ORDERING.md#11-method)). This block holds mostly logic and few
numbers, and the numbers it has were already moved by the tuning of 8.0, so step 1 (our form, identical tree) is
enough for most of it; new terms, such as a stand-pat margin per expert, start off and get their own SPRT. With one
thread the check is an identical bench. With more threads the search is not deterministic, so a change of behaviour
is checked by an SPRT at 4 threads and by the scaling of the speed.

## 5.3 Threads (rewritten, 17 October 2026)

The parallel search is a lazy SMP, the common scheme of most engines: every thread searches the same root with its
own move ordering statistics, and the threads share the transposition table, the pawn-structure history and the
correction histories. The thread infrastructure was already our own (threads that live for the whole session and
wait on a condition variable, 12 October). Rewritten now, at unchanged behaviour:

| Parameter | What it does | Start | Change |
|---|---|---:|---|
| `SmpVoteBase`, `SmpVoteScore` | the support a thread gives its move: (score − lowest score among the threads + base) × weight | 14, 1024 | formula: the support of a move is computed once, the weight is a parameter |
| `SmpVoteDepth` | support added per completed ply, so a deeper thread counts more | 0 | formula: a new term, additive |
| `SmpAspCycle`, `SmpAspStep` | the aspiration window of each thread is offset by (thread id mod cycle) × step, so the threads search slightly different windows | 8, 1 | split |
| `SmpSkew` | the odd helper threads start this many plies ahead, so the threads finish their iterations at different times | 0 | a known variant of lazy SMP, off |

A decided and complete result (a mate or a tablebase score without a bound) still wins the vote, and equal support
goes to the longer line. With one thread the bench is identical (137419). Both new terms are known ideas in a form of
our own (staggered helper threads and a depth-weighted vote appear in other engines), so they do not enter the
register of novelties. How the code works is in the [block 5 code map](../../conceptual_maps/BLOCK_5_CODE_MAP.md).

**Test.** The two terms on (`SmpSkew=1`, `SmpVoteDepth=10`) against the defaults, which reproduce the old behaviour;
4 threads per engine, 2+0.05, ten games at a time on each socket:

| Games | Elo |
|---|---:|
| with hyperthreading, ten games at a time per socket: 4,210 | +1.32 ± 5.33 |
| without hyperthreading, five games at a time per socket: 1,438 | +2.90 ± 9.18 |
| **all** 5,648 | **+1.72 ± 4.61** |

No game lost on time. The terms do no harm, and a gain, if there is one, is small at this time control; the test
was stopped before a decision. The rewrite of the threads stays, with the old behaviour at the defaults, and the two
terms stay off: they are tested again at the end of the 8.1 queue, at 8 seconds and at 8 threads (section 5.5).

Status: threads rewritten; new terms off, queued.

## 5.4 Aspiration windows (rewritten, 17 October 2026)

Every iteration searches the root in a narrow window around the average score of the move. A result outside the
window is searched again with a wider one: after a fail low the upper bound comes down and the lower bound goes below
the result; after a fail high the lower bound goes up, the upper bound above the result, and each fail high in a row
takes plies off the next search. Rewritten at an identical tree:

| Parameter | What it does | Start | Change |
|---|---|---:|---|
| `AspGrowLo`, `AspGrowHi` | growth of the window after a fail low and after a fail high (one factor served both) | 51, 51 (/128) | formula |
| `AspFhRed` | plies taken off for each fail high in a row | 1 | split |
| `AspLoKeep` | after a fail low, the upper bound keeps this share of the window instead of falling to the lower bound | 0 (/1024) | new, off |

## 5.5 Tests and tuning of block 5

The rewrite keeps the old behaviour at the starting values, so it needs no test of its own. Every new lever does:

1. **An SPRT before it is switched on**, against the defaults: at one thread for the levers of the single search
   (`AspLoKeep`), at 4 threads for those of the parallel search (`SmpSkew`, `SmpVoteDepth`), then at 8 threads.
2. **An SPSA** for the levers that pass, in the tuning pass of the block; where the right value may change with the
   material on the board (the growth of the aspiration window, the plies taken off after a fail high), the lever is
   split per expert before the run.

| Lever | Block part | SPRT | SPSA |
|---|---|---|---|
| `SmpSkew`, `SmpVoteDepth` | threads | first test level (+1.72 ± 4.61, 5,648 games, 4 threads, 2+0.05); again at the end of the queue, 8 s and 8 threads | if it passes, with `SmpVoteBase`, `SmpVoteScore`, `SmpAspStep` |
| `AspLoKeep` | aspiration | at the end of the queue | if it passes, with `AspGrowLo`, `AspGrowHi`, `AspFhRed`, possibly per expert |

Quiescence and the use of the transposition table follow, rewritten at an identical tree.
