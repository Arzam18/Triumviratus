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
