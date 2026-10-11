# nnue/ — Triumviratus NNUE inference (TRANN3, Consilium)

This directory holds the inference code of **TRANN3**, the architecture of Consilium, Triumviratus' own network: four
input blocks (FullThreats, HalfKAv2_hm with four phase experts, PawnPair, PassedPawns), L1 = 1024, 8 layer stacks.
The net format is our own; no Stockfish net can be loaded.

**Origin.** The code started from the **Stockfish NNUE implementation**
(https://github.com/official-stockfish/Stockfish, GPLv3, SFNNv16 line; see COPYING at the repo root). Since then most
of the part that runs at every node has been rewritten for Consilium, while the forward pass and the loading code are
still Stockfish's. A line-by-line comparison with Stockfish 19 and with our 7.0 port (October 2026,
`docs/audit_8.0/AF_ATTRIBUZIONE_INFERENZA.md` in the development tree) gives:

| part | lines in common with Stockfish | |
|---|---:|---|
| accumulators (`nnue_accumulator.*`) | 15–28% | rewritten: per-phase refresh tables, pawn-block cache, hybrid king update, the engine's board read directly |
| feature blocks (`features/`) | 36–39% | FullThreats and HalfKAv2_hm modified; PawnPair, PassedPawns and the feature permutation ours |
| link with the search (`../nnue_bridge.*`, `nn_board.h`, `../nn_dirty.h`) | about 5% | ours |
| forward pass (feature transformer, affine and sparse layers, activations, SIMD) | 82–98% | Stockfish's, with small changes |
| support code (net loading, memory, misc, types, tune) | 85–98% | Stockfish's |

Overall about 60–70% of the lines are still in common with Stockfish (about 40–50% of the code that runs at every
node). Credit and copyright for the original machinery belong to the Stockfish developers: their license headers are
kept in every file derived from their code, and the namespace is `Triumviratus::` (renamed from `Stockfish::` on
13 July 2026) only to keep it apart from the engine's own globals. This is a GPLv3 derivative work.

**Our contributions in this directory.**
- **Consilium's mixture of experts**: four phase experts in the king-relative block, with per-phase accumulator
  refresh tables.
- **PassedPawns** (`features/passed_pawns.*`): one input per passed pawn, an original feature of this project.
- **PawnPair** (`features/pawn_pair.*`): our implementation of the pawn-pair feature invented for Pawnocchio (see the
  repository README for its attribution).
- **One board**: the network reads the search's board through `nn_board.h`, and the engine's make writes the changes
  of each move (`../nn_dirty.h`); there is no second board.
- Incremental threat accumulator (`TRIUMV5_THREATS_INCR`), pawn-block cache, speed work on the accumulator update,
  embedded-net loading via a Windows RCDATA resource (`TRIUMV_EMBED_RESOURCE`).
