# Search rewrite, block 1: histories and move ordering

Part of the 8.1 plan in [`DEVELOPMENT_8.1.md`](../../../DEVELOPMENT_8.1.md). This block covers the statistics the
search keeps about moves (quiet history, continuation history, pawn history, capture history) and the order in which
the move loop tries quiet moves. It comes first because the later blocks read these statistics: the reductions use
the history of a move to decide how much to reduce it, and pruning uses it to decide which moves to skip.

Every change in this block follows the same cycle: a written design, code behind an option (off, the search tree is
identical, checked by the bench), a non-regression SPRT at unchanged parameters, then the old code is removed. The
network has four experts chosen by material, so each design asks whether one weight fits all four or each expert
needs its own.

## 1.1 A quiet history indexed by threat context

The same quiet move means different things when the piece is escaping a threat, when it walks into one, or when
neither square is threatened. The history of quiet moves was indexed by the move alone (from-square and to-square),
so these cases shared one entry.

**Design.** Each quiet move gets a two-bit context: the first bit says whether its from-square is threatened, the
second whether its to-square is. A square is threatened for a piece when an enemy piece of lower value attacks it,
the attack that costs material even if the square is defended: for a pawn, an enemy pawn; for a knight or a bishop,
a pawn; for a rook, a pawn or a minor piece; for a queen, also a rook. The king has no context. The threat maps are
the ones the move orderer already builds, so the context costs one lookup per move. A second form, kept for
comparison, uses the known definition: a square attacked by any enemy piece.

In the first version (`ThreatHist=1`) each context had its own history table: the move indexed one of four tables.

| Version | Time control | Games | Elo | Decision |
|---|---|---:|---:|---|
| `ThreatHist=1`, our definition | 8+0.08, hyperthreading | 33,510 | −3.32 ± 1.93 | H0 |
| `ThreatHist=1`, our definition | 15+0.15 | 2,892 | +0.48 ± 6.37 | stopped, level |
| `ThreatHist=2`, any attack | 10+0.1, hyperthreading | 1,804 | −1.16 ± 8.27 | stopped, undecided |

**Why the first version lost.** Three causes, read from the code:

1. The move orderer already had a fixed threat term (`ThreatOrderMul`) built on the same lower-value attack maps: a
   bonus for a piece leaving a threatened square, a malus for one entering it. The context repeated information the
   orderer already had.
2. The updates of one move were divided among four tables. The three threatened contexts are rare, so their tables
   learned from few examples, and the shared table lost exactly those updates.
3. The history with context also entered the statistic score of the reductions and the history pruning, whose
   parameters were tuned on the single table.

## 1.2 The factored history

**Design** (`ThreatHist=3`), a design of our own, built on the common pattern of summed history tables. The single
table receives every update, as before. Three small correction tables, one per threatened context, learn only how
much better or worse the move does in that context than its overall history says. The orderer reads the sum of the
two; the reductions and the history pruning read the single table only, so their tuned parameters keep their
meaning. Two parameters weigh the correction: `ThreatCtxW`, its weight in the ordering score, and `ThreatCtxUpd`, the
scale of its update (1024 = 1.0).

With the starting weights: **−3.69 ± 4.41 over 6,212 games** at 8+0.08. The weights were chosen by hand, and the
fixed threat term was tuned before the correction existed, so a tuning run follows before any verdict.

**Tuning.** An SPSA run on the three parameters (`ThreatCtxW`, `ThreatCtxUpd`, `ThreatOrderMul`) with the factored
history on, 8+0.08, in progress. After about 1,000 iterations the fixed threat term moves steadily from 20 toward
25, while the two correction weights stay near their starting values.

**Acceptance rule**, fixed before the tests. Two SPRTs against the current engine: A, the factored history with the
tuned vector; B, the single history with only the tuned `ThreatOrderMul`. B tells how much of the gain belongs to the
fixed threat term alone. The factored history is adopted if A does not lose against the current engine and A is
within 2 Elo of B: a small, declared price for a block of our own that the later blocks build on.

## 1.3 The threat balance of a quiet move (in progress)

The ordering score of a quiet move adds about twelve terms, each with its own weight. Three of them describe the
same thing, the threats the move changes: the fixed threat term (`ThreatOrderMul`, leaving or entering a threatened
square) and the attack bonuses (`AttackOrder`, `AttackOrderQ`, attacking a target from a safe square).

**Design.** One term replaces the three: the threat balance of the move, in units of material,

> value saved (the piece leaves a threatened square) − value exposed (it enters one) + a share of the value of the
> best target it attacks from a safe square,

computed from the maps the node already builds; the value of the target is looked up only for the few moves that
attack one. One weight per expert scales the balance, since the value of a threat changes with the material on the
board, and one parameter sets the share of the target. The positional terms (outposts, king shield, passed-pawn
pushes) stay as they are. The same balance will feed the reductions of block 2.

Status: code behind an option, off by default.
