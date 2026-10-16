# Triumviratus 8.1: development log

The work toward 8.1, on the `dev` branch. The record of 8.0 is in [`DEVELOPMENT_8.0.md`](docs/development/archive/DEVELOPMENT_8.0.md); the
rules for contributions are in [`CONTRIBUTING.md`](docs/project/CONTRIBUTING.md).

## Plan

8.1 keeps the Consilium network of 8.0: all the work is on the engine side. The order follows one principle: the
structure of the search first, then the tuning of its parameters, because a tuning made before a rewrite would no
longer fit the rewritten code.

**Goal.** 8.1 shares no line of code with Stockfish, with no loss of strength. The search and the inference of the
network are rewritten block by block; every block passes a non-regression SPRT before the old code is removed. Known
ideas stay, credited in the documents; the code and its formulas are our own.

**Measurement rules**, as in 8.0: one test at a time on the test machine; every search change by an SPRT run to a
decision, with at least 20,000 games; a short time control first, then a long one for the changes that pass; small
levers go to SPSA rather than to single SPRTs; speed only with hardware counters at an identical search tree, the
engine pinned to one core.

**In order:**

1. **The base.** A non-regression SPRT of `dev` (pull request 6 and the prefetch limited to non-AVX-512 builds)
   against 8.0; if it passes, `dev` is the base of 8.1. For the release build, an option to import a PGO profile
   trained on another machine: Zen 4 trains variants 1 to 6 natively, an Intel processor with AVX-VNNI trains
   variant 7. The gain of native profiles for `vnni512` and `avx512icl` is measured on Zen 4 first.
2. **Where we lose.** A study of the games 8.0 loses against the strongest engines (CCRL and TorsGUI games): the
   phase of the game, which expert of the network misjudged the position, the time left; the use of time at the CCRL
   controls (40/15 with increment); scaling with 4 and 8 threads, which matters for the 4CPU
   lists; the search in endgames and the use of tablebases. It runs on stored games and does not need the test
   machine, so it starts at once. Its result says which search components to rewrite first.
3. **A search of our own** ([future directions, section 11](docs/project/FUTURE_DIRECTIONS.md#11-a-search-shaped-by-the-network)).
   A rewrite of the search one component at a time, designed for a network of experts, each with a non-regression
   SPRT at unchanged parameters. Six blocks; the first four in this order, because each one feeds the next:
   1. **histories and move ordering**: a history of quiet moves indexed by the threat context of the move, in a form
      of our own built on the value of the attacking pieces, and the ordering rethought around it
      ([block 1](docs/development/8.1/SEARCH_1_ORDERING.md));
   2. **reductions**: one formula of our own on our signals (the expert of the network, the causal reduction, the
      rank of the move's ordering score) ([block 2](docs/development/8.1/SEARCH_2_REDUCTIONS.md));
   3. **pruning**: reverse futility, null move, futility, ProbCut and losing captures, with per-expert margins where
      they pay ([block 3](docs/development/8.1/SEARCH_3_PRUNING.md));
   4. **extensions**: singular extension, the low-depth hash-move extension and internal reduction in one design
      ([block 4](docs/development/8.1/SEARCH_4_EXTENSIONS.md));
   5. **the core of the search**: quiescence, the use of the transposition table, iterative deepening with aspiration
      windows, and the threads ([block 5](docs/development/8.1/SEARCH_5_CORE.md));
   6. **the inference of the network**: the forward pass, the SIMD layers and the loading of the network, rewritten to
      give bit-identical evaluations, so that strength is unchanged by construction and only speed is measured
      ([block 6](docs/development/8.1/NNUE_INFERENCE.md)).

   Each stage: a written design, code behind an option (off, the tree is unchanged), a non-regression SPRT, then the
   old code is removed. Each block has its own document with the design, the tests and their results. Time management
   runs as a fifth line in parallel ([time management](docs/development/8.1/TIME_MANAGEMENT.md)).
4. **Tuning with the corrected schedule** ([section 8](docs/project/FUTURE_DIRECTIONS.md#8-tuning-again-with-the-corrected-schedule)).
   The six SPSA runs P1 to P6 on the final structure, without hyperthreading; each vector enters the engine only
   through an SPRT against the release.
5. **Release of 8.1.**

**In parallel:**

- **The open measurements of 8.0** ([sections 6, 7 and 12](docs/project/FUTURE_DIRECTIONS.md#7-open-engine-measurements)).
  The time-management ones (time per expert, the graded surprise rule, the two defence rules) do not depend on the
  search and run whenever the machine is free. The search ones (LDSE, reduction scale per expert, `DrawDefend`, the
  null-move guard from the analysis build) are taken up inside step 3, with the rewrite of the component they touch.
- **Speed.** A Tin Man continues his work (a legal generator with the AVX-512 compacted writing is the first
  proposal); every pull request is measured on the Xeon, on Zen 4 and on an Intel processor with AVX-VNNI. Changes
  at an identical tree do not touch the parameters, so they do not disturb steps 3 and 4.
- **The move generator and the search.** The legal generator gives the exact number of legal moves at every node at
  no cost. Known search ideas built on that count are tested inside the stages of step 3 that they touch: reductions
  and late-move pruning scaled by the legal count (stages 2 and 3); an extension when only one move is legal, with no
  singular verification search in that case, and no check extension when the evasions are many (stage 4); a
  null-move guard when few moves are legal; mate and stalemate recognised in quiescence; an instant move with a
  single legal reply (time management). If they gain, the legal generator is used on every build and the search
  builds on the count. If they do not, both generators stay and each variant of the universal executable uses the
  faster one: the two give the same moves in the same order, so the bench is the same on every build.
- **For users.** Playing strength levels (`UCI_LimitStrength`, `UCI_Elo`, calibrated in games,
  [section 13](docs/project/FUTURE_DIRECTIONS.md#13-playing-strength-levels)) and Chess960 in the release build,
  which today has it only in development builds; with it the engine can enter the CCRL FRC list.
- **Machines.** The test machine also plays CCRL tournaments: an OpenBench instance with the testers' machines would
  let SPRT and SPSA runs use them too.

## 1. Speed at an identical tree: the first external contribution (15 October 2026)

Pull request 6, by **A Tin Man**, merged into `dev` on 15 October. Six changes, all keeping the search tree identical
(bench 137419 in every build the author could run; the check on the pull request confirmed it):

| # | change | where it acts |
|---|---|---|
| 1 | **Legal move generator** (`source/movegen_legal.h`), ported from the author's engine [Zoomies](https://github.com/CodeAdopter/Zoomies) (MIT; its generator descends from [Surge](https://github.com/nkarve/surge), MIT) and written to give the moves in exactly the order of our generator. Generated moves skip the legality test before the move and the king test of the make; the hash move keeps the old checks; Chess960 keeps the staged generator. `tdperft` compares the two generators at every node | every build |
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

**Measurements on the project's machines.** Two profile-guided universal executables of the same batch (clang-cl,
deterministic PGO training), built from `dev` before the pull request (47ea7b9) and after the merge (a2e06ed); bench
137419 in every variant the build machine can run. A third engine, `base_copia`, is the same file as the first under
another name: its difference from `base` is the noise of the measurement.

Hardware counters (xperf), Xeon Gold 6138, `avx512` variant, engine pinned to one processor, 30 positions at 400,000
nodes, 4 rounds in alternating order:

| | cycles/node, middlegame | cycles/node, endgames | instructions/node | branch mispredictions/node |
|---|---:|---:|---:|---:|
| pull request against base | +0.36% | +0.40% | −0.54% / −0.81% | +2.2% / +1.9% |
| noise (`base_copia` against base) | +0.14% | +0.45% | +0.03% / −0.02% | +0.05% / +0.03% |

Raw speed, `bench 18` (2,840,711 nodes) repeated with the three engines in rotating order, engine pinned to one
processor; mean of the per-round ratios with its 95% interval:

| machine | variant | rounds | pull request against base | noise |
|---|---|---:|---:|---:|
| Xeon Gold 6138 | `avx512` | 30 | +0.32% [−0.92, +1.57] | −0.95% [−3.45, +1.56] |
| Xeon Gold 6138 | `avx2` (forced, `TRIUMV_ISA=avx2`) | 20 | **+1.03%** [+0.19, +1.86] | −0.32% [−1.11, +0.46] |
| Zen 4 laptop | `vnni512` | 30 | +0.81% [−0.82, +2.44] | −0.88% [−2.38, +0.62] |
| Intel Core 7 150U (Raptor Lake, laptop) | `avxvnni` (base: `avx2`) | 30 | **+6.34%** [+3.76, +8.92] | +0.82% [−2.26, +3.89] |

On processors with AVX-512 only changes 1 to 3 act, and the result is level or slightly positive: the AVX-512 build
already wrote the generated moves with `vpcompressd` (−1.5% of cycles, [8.0 log §20](docs/development/archive/DEVELOPMENT_8.0.md)), so
the legal generator saves instructions there but adds branch mispredictions. On the AVX2 path the gain is real but
smaller than on the author's machines (+1.0% against +3.6% on Raptor Lake), probably because the profile-guided build
already arranges the branches that the changes remove. On an Intel processor of the Raptor Lake family the
universal executable picks the new AVX-VNNI variant, and the gain with release-style builds is +6.3%, against the
author's +9.2% without PGO; the node counts of the bench are identical in that variant too. The AVX-VNNI variant of
these builds is trained on the Xeon, which lacks AVX-VNNI, with AVX2 instructions. Trained on the Core 7 150U
itself, with its own instructions (`build_universal.ps1 -Only 7`, the same deterministic training), the variant gains
more:

| Intel Core 7 150U, `bench 18`, 30 rounds | against base (`avx2`) |
|---|---:|
| universal executable, `avxvnni` trained on the Xeon | +5.51% [+4.06, +6.96] |
| `avxvnni` alone, trained on the 150U | **+6.84%** [+6.02, +7.66] |

A native profile adds about 1.3%, and the speed is steadier from round to round (lowest round 992,908 nodes per
second against 835,503). The 8.1 release will therefore train the AVX-VNNI variant on a processor that has it and
bring that profile into the universal build; the build script needs an option to import a profile trained elsewhere. The change stays: it costs nothing on AVX-512 and helps the processors where it
acts.

**Each change on its own, Xeon.** Five `avx512` builds with PGO from the same batch (base, the full pull request,
and the pull request without one change each), xperf with the engine pinned, 4 rounds; cycles per node against the
full pull request:

| | middlegame | endgames |
|---|---:|---:|
| without the next-move prefetch | **−0.40%** | **−0.72%** |
| with the old staged generator | +0.26% | −0.22% |
| with the evaluation cache back | +0.19% | +0.32% |
| noise (`base_copia` against base) | 0.28% | 0.19% |

The prefetch costs on the dual-socket Xeon in both workloads, and without it the branch mispredictions return to the
level of the base: it is now off in the AVX-512 builds (avx512, vnni512, avx512icl) and stays on where it was
measured, on Intel processors without AVX-512. Removing the evaluation cache helps here too. The legal generator and
the old one are level on AVX-512: the legal generator saves the legality tests, while the old one wrote the moves of a
piece sixteen at a time with `vpcompressd`, which the legal generator does not do. A legal generator with that
compacted writing might take both gains on AVX-512 processors; it is proposed to the author as a next step. Which
generator each build uses in the end depends on the search ideas built on the legal move count (see the plan).

**Node counts.** The bench is identical, but in a workload of 30 positions searched one after another in the same
process, without `ucinewgame` in between, the pull request searches 12,008,338 nodes per round against 12,008,522,
the same difference in every round. The likely cause is the removed evaluation cache: a hit stored with a different
fifty-move count returned a value that could differ by rounding from a fresh evaluation, so the old tree depended on
what the cache held. The difference is 15 nodes in a million and does not matter for strength; the tree is identical
when the cache is cold, as in the bench.
