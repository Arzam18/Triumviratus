# Search rewrite, block 2: reductions

Part of the 8.1 plan in [`DEVELOPMENT_8.1.md`](../../../DEVELOPMENT_8.1.md). This block covers late-move reductions:
how much shallower than the nominal depth each move after the first is searched, and when a reduced move is searched
again at full depth.

## 2.1 The current form

The reduction starts from a base that grows with the logarithm of the depth and of the move number, and about
nineteen terms are added to it one at a time: node type (principal variation, expected cut or all node, a
principal-variation hash entry), improvement of the static evaluation, the size of the evaluation correction, the
statistic score of the move, captures and checks, cutoffs found by the children. Three terms are already our own: a
level per expert of the network, the causal reduction (a quiet move that does not touch the squares of the refutation
of a sibling that failed low is reduced more), and a smaller reduction for passed-pawn pushes in endgames.

Each term was tuned with the others in place, so none can be changed alone without moving the balance of the rest.

## 2.2 Design: three groups

The rewrite gathers the terms into three groups, each with few weights:

1. **How promising the node is.** Principal variation, hash entry, expected cut or all node, improvement and the
   evaluation correction, in one term.
2. **How promising the move is.** In place of the move number, the rank of the move's ordering score among its
   siblings: move ten with a score close to the first is not move ten at the end of a tail of poor moves. With it,
   the threat balance of the move from block 1 and its statistic score. When the legal move generator provides the
   exact number of legal moves, the move number can also be read as a fraction of them (a known idea, proposed by A Tin
   Man together with the legal generator).
3. **The signals of the network.** The level per expert and the causal reduction, already ours. Whether the slope of
   the reduction should also change with the expert is measured here; an earlier try of a per-expert slope alone was
   neutral, so it enters only if the new form gives it a role.

The rank of the ordering score is the part with the most risk and the most potential: it ties the reduction to the
same information that orders the moves, so a better order also means better reductions.

## 2.3 First piece: the rank of the ordering score (`LmrRank`)

**Code.** For a quiet move taken from the sorted list, the move queue now passes its ordering score and the score of
the best quiet move of the node. The base reduction reads an effective move number: the number of the first quiet
move of the node, plus one move for every `LmrRankScale` points of distance from the best score. It is mixed with the
ordinary move number by one weight per expert (`LmrRankMix0` to `LmrRankMix3`, 1024 = effective number only). With
`LmrRank` off the tree is identical.

**Untuned**, starting values set by hand (scale 4000, mix one half for every expert), on top of the factored history
of block 1, without hyperthreading at 8+0.08: **−0.70 ± 5.73 over 3,478 games**, level.

**Tuning, coarse.** An SPSA run on the five parameters (8+0.08, 8 games per iteration). After 300 iterations the mix
of the endgame expert (nine pieces or fewer) fell steadily from 512 toward 430: with few quiet moves and close
ordering scores, the distance says less than the move number. With the coarse vector the package lost
**−4.34 ± 5.67 over 3,602 games** (8+0.08, no hyperthreading), so the vector was set aside.

Status: off. `LmrRank` adds a signal without replacing any inherited formula, so it waits for the rewrite of this
block, where it is tuned again with the other terms at a longer time control and 16 games per iteration: effects of
one or two Elo on the reductions are below what an 8-game iteration at 8 seconds can resolve.
