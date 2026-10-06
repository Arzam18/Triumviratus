# nnue/ — Triumviratus NNUE (derived from Stockfish)

This directory is **derived from the Stockfish NNUE implementation**
(https://github.com/official-stockfish/Stockfish, GPLv3 — see COPYING at the
repo root and the license headers kept verbatim in every file). Credit and
copyright for the original machinery belong to the Stockfish developers.

Triumviratus-specific contributions on top of the Stockfish base:
- **Inference reworked for the Consilium mixture-of-experts network** (8.0): four
  phase experts on the king-relative block (`TRIUMV_PSQ_PHASES=4`), the
  PassedPawns block, refresh caches for the pawn blocks and for phase changes,
  row permutation for cache locality, and many measured speed changes.
- **PawnPair feature block** (`nnue/features/pawn_pair.*`): 4560 pawn-structure
  inputs grafted as a third feature block (Full_Threats + HalfKAv2_hm + PawnPair),
  with the composed serialization/hash handled across trainer and engine.
- Incremental threat-accumulator drive (`TRIUMV5_THREATS_INCR`), position_min
  reduction, Mens visualizer instrumentation, embedded-net loading via Windows
  RCDATA resource (`TRIUMV_EMBED_RESOURCE`), and assorted integration glue for
  the Triumviratus search core.

Because the network code now diverges from upstream (own feature block, own
nets, own binary format hash), the namespace was renamed `Stockfish::` →
`Triumviratus::` (2026-07-13) to make the divergence explicit. This is a
GPLv3 derivative work; all original notices are preserved.
