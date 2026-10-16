# Search rewrite, block 3: pruning

Part of the 8.1 plan in [`DEVELOPMENT_8.1.md`](../../../DEVELOPMENT_8.1.md). This block covers the rules that cut a
node or a move without searching it fully: reverse futility pruning, the null move, razoring, futility pruning of
quiet moves, ProbCut, and the pruning of losing captures.

## 3.1 Starting point

Reverse futility and futility pruning already have margins per expert of the network (`RfpExp`, `FutExp`), tuned in
8.0. The other rules use one margin for all four experts.

## 3.2 Directions

- **Margins per expert where they pay.** The confidence in the static evaluation differs between the experts, so the
  margin that is safe in a middlegame expert may be too wide or too narrow in an endgame one. Each rule gets per-expert
  margins only if an SPSA moves them apart and an SPRT confirms the gain.
- **Known ideas to measure.** Razoring with a margin linear in depth, disabled while the search is looking for a mate
  (Stockfish, 2026); an earlier linear form of ours, limited to all nodes and without the mate guard, lost −2.32 ±
  2.52 over 20,032 games. A null-move guard when the side to move has very few legal moves, which the legal generator
  makes cheap to check.
- **ProbCut with an adaptive depth**, tied to the reductions of block 2.

Status: planned, after block 2.
