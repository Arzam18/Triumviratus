# Triumviratus 8.1: development log

The work toward 8.1, on the `dev` branch. The record of 8.0 is in [`DEVELOPMENT_8.0.md`](DEVELOPMENT_8.0.md); the
rules for contributions are in [`CONTRIBUTING.md`](CONTRIBUTING.md).

## 1. Speed at an identical tree: the first external contribution (15 October 2026)

Pull request 6, by **A Tin Man**, merged into `dev` on 15 October. Six changes, all keeping the search tree identical
(bench 137419 in every build the author could run; the check on the pull request confirmed it):

| # | change | where it acts |
|---|---|---|
| 1 | **Legal move generator** (`source/movegen_legal.h`), ported from the author's engine Zoomini and written to give the moves in exactly the order of our generator. Generated moves skip the legality test before the move and the king test of the make; the hash move keeps the old checks; Chess960 keeps the staged generator. `tdperft` compares the two generators at every node | every build |
| 2 | **Per-thread evaluation cache removed**: 12 hits in 2.5 million lookups at bench depth 20, 62 in 9.4 million over a 40-ply game. The transposition table already stores the evaluation | every build |
| 3 | **Prefetch of the hash entry of the next move's child**, decided at start-up from the processor vendor: on for Intel, off for AMD, where it loses | Intel |
| 4 | AVX2 rank counting of the move ordering in full groups of 8, as the AVX-512 path already did | AVX2 builds |
| 5 | Threat rows of the network widened without `vpmovsxbw`, through a byte interleave at load | `avx2-nopext`, `avxvnni` |
| 6 | **AVX-VNNI variant** (variant 7 of the universal executable, `make avxvnni`): the dot products of the network with `vpdpbusd` in its 256-bit form, for Intel processors from Alder Lake on without AVX-512. `TRIUMV_ISA` now checks a forced variant against the processor's features | Intel 12th generation and later |

The evaluation cache could be removed without changing the tree because it stores the raw output of the network,
which depends only on the position. An early note of the 8.0 log (section 5) said that dropping it changed the tree:
that was true of an older design, which stored evaluations together with the optimism of the iteration.

**The author's measurements**, whole series against `dev`, nodes identical, g++ with link-time optimisation and no PGO:

| CPU | build | gain |
|---|---|---:|
| Intel i9-14900KF (Raptor Lake) | `avxvnni` (chosen there by the universal executable) | **+9.17%** [+8.76, +9.58] |
| Intel i9-14900KF | `avx2` | +3.63% [+3.36, +3.91] |
| AMD Ryzen 7 5700 (Zen 3) | `avx2` | +5.00% [+3.29, +6.75] |
| AMD Ryzen 7 2700X (Zen+) | `avx2-nopext` | +3.23% [+2.66, +3.80] |

**Measurements on the project's machines**, profile-guided universal builds of the same batch (before and after the
pull request), hardware counters with the engine pinned to one processor: Xeon Gold 6138 (`avx512` variant) and
Zen 4 (`vnni512`). Results to follow.
