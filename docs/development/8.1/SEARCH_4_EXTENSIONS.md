# Search rewrite, block 4: extensions

Part of the 8.1 plan in [`DEVELOPMENT_8.1.md`](../../../DEVELOPMENT_8.1.md). This block covers the rules that search
a move deeper than the nominal depth: the singular extension, the low-depth hash-move extension, and the internal
reduction of nodes without a usable hash move.

## 4.1 Direction

The three rules decide the same question from different sides (is this node worth more depth or less?), and today
each has its own conditions and margins. The rewrite gives them one design, so that a node is not extended by one rule
and reduced by another for the same reason.

**Known ideas to measure**, made cheap by the exact legal move count of the legal generator: an extension when the
side to move has a single legal move, with no singular verification search in that case (a single move is singular
by definition), and no check extension when the evasions are many.

Status: planned, after block 3.

## 4.2 Rewritten in our form (19 October 2026)

The singular extension is in our code at the tree of 8.0 (bench unchanged). The hash move is tested for being the only
good move: the other moves are searched at about half depth against a bar below the hash value. Three outcomes: the
hash move is extended, by more plies the further the others stay under the bar; the exclusion search clears beta and
the node returns (several good moves); or the hash move is searched shallower when the node expects a cutoff.

- The thresholds of the previous code are parameters at their old values: `SingMinDepth` 6 (+`SingMinDepthTtPv` on
  hash entries of the principal variation), `SingHashSlack` 3, `SingMarginDiv` 63, `SingDepthMul` (half depth).
- The terms shared by the double and the triple extension are computed once.
- The negative extension is split by its two reasons: `SingNegHigh` (the hash value clears beta) and `SingNegCut`
  (expected cut node), both 3.
- Terms of our own, at 0: `SingExp0`–`3` (margin of the bar per expert of the network, the idea of the per-expert
  margins that gained in 8.0), `SingSurplus` (margin per ply of hash depth beyond what is needed), `SingMoving`
  (margin per 64 units between the hash value and the static evaluation: a node in motion trusts its hash less),
  `SingLvlExp0`–`3` (thresholds of the extra plies per expert).
- Already ours: `Ldse` (extension of the hash move near the horizon when the evaluation is below alpha) and the cap on
  extensions from negative reductions along a line.

## 4.3 Status and remaining steps (19 October 2026)

1. Dev build and commit of the rewrite (bench 126352).
2. Long tuning with the new terms; until it moves them, the logic keeps the algorithmic base of Stockfish 19.
3. Bake with a long non-regression test.
