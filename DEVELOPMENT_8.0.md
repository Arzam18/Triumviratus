<div align="center">

<img src="logo.png" alt="Triumviratus" width="200">

# Triumviratus 8.0 — development log

**Started as a speed project.** The code around the network made faster · ablations · then a new network, Consilium · still in progress

**by Francesco Torsello**

<sub>in collaboration with Maurizio Platino</sub>

</div>

---

<div align="center">

[Why speed](#1-why-speed) · [How it is measured](#2-how-it-is-measured) ·
[Where we started](#3-where-we-started) · [What changed](#4-what-changed-identical-tree) ·
[Tried and dropped](#5-tried-and-dropped) · [TT16](#6-tt16-the-one-change-that-alters-the-tree) ·
[Result](#7-result-against-70) · [Ablations](#8-ablations-switching-off-instead-of-adding) · [MoE network](#9-the-80-network-consilium-against-70) · [Endgame depth](#10-endgame-depth-study) · [Status](#11-status) · [7.0 log](DEVELOPMENT_7.0.md)

</div>

---

> [!NOTE]
> **Work in progress.** `source/` now holds the 8.0 development code; the 7.0 release is the tag
> `v7.0`. Every change in section 4 leaves the search tree **bit-for-bit identical** (same `bench`,
> same node counts on 50 positions at depth 15), so it can only change speed, never play. Against the
> official 7.0 binary, the same tree now runs **+8.7% faster**, and **+11.5%** with the new
> transposition table (section 6). In games, 8.0 beats the 7.0 release by **+14.7 ± 5.4 Elo** at
> 12+0.12 and, with two search features switched off after ablation tests (section 8), by
> **+11.7 ± 4.6 Elo at 60+0.6** (section 7). With the final **Consilium network** and its SPSA baked
> into the code, the 8.0 release build beats the official 7.0 by **+27.3 ± 8.3 Elo** at 15+0.15 over
> 2,000 games (section 9). A broader search SPSA is next, then the release.

---

## 1. Why speed

8.0 started with an audit of the search against the engines released in the last few months:
Stockfish 19, Reckless, PlentyChess 8, Integral 8, Caissa 2.0, Stormphrax 8 and Viridithas 20.
Porting their search ideas did **not** pay here. Three SPRTs in a row came back flat or negative:
dropping null-move pruning inside the singular search (−23 ± 19, stopped early), fail-high score
blending (−0.6 ± 7.3 on 2,502 games) and a bundle of five small ports (−4.4 ± 8.0 on 4,212 games).
Our search is tuned around its own behaviour, so a single foreign heuristic mostly shifts the balance
the tuning found.

Speed has no such problem: if the tree is identical, a faster engine is simply stronger. So the audit
turned to the question "where does our time go, compared with Stockfish?"

## 2. How it is measured

Two tools made this possible on a machine that was busy with other simulations the whole time (wall
clock varied ±20% between identical runs).

**Hardware counters.** Windows `xperf` reads the CPU's performance counters per process:
instructions retired (exact, unaffected by load), cycles, branch mispredictions and last-level cache
misses, plus samples attributed to source lines through the debug symbols. Stockfish 19 uses the
**same network dimensions** as ours (SFNNv16, L1 = 1024), so building both with the same compiler
(g++ 16, `-O3 -flto`) and running both on the same positions gives a direct, per-node comparison.

**Paired simultaneous runs** (`build/nps_pair.py`). To measure time under load, the two binaries run
**at the same moment on the two hyperthreads of one physical core**, same position, same fixed node
count, timed from outside. Whatever disturbs one disturbs the other. The processes are created
suspended and pinned before their first instruction (so the network lands in the right NUMA node's
memory); whoever finishes first keeps searching until the other is done (so neither ever has the core
to itself); the side and the start order alternate. Six cores in parallel give 360 samples in about a
minute. **Null test** (the same binary against itself): **−0.03%, 95% interval [−0.30%, +0.25%]**.

## 3. Where we started

Same compiler, same 30 positions × 400,000 nodes, per node:

| engine | instructions | cycles | branch misses |
|---|---:|---:|---:|
| Stockfish 19 | 6,087 | 6,678 | 28.7 |
| **Triumviratus 8.0 at the start** | **6,768** | **8,728** | **37.7** |

We did not execute many more instructions than Stockfish: the gap was **stalls** — about 30% more
branch mispredictions and more memory misses. The network code itself (accumulator updates and the
forward pass) cost the same as Stockfish's; the difference was all **around** it: move ordering,
correction history, the transposition table.

## 4. What changed (identical tree)

| change | effect |
|---|---|
| Minor/major-piece keys of the correction history kept **incrementally** in make/unmake, instead of rescanning eight bitboards up to four times per node | together with the next two: **−7.5% instructions** |
| The non-pawn key is no longer updated when the feature that uses it is off | |
| Three `thread_local` values in the NNUE bridge moved into the per-thread state | |
| **Branch-free move selection** (conditional moves) for quiets, captures and quiescence: selecting the best quiet alone caused ~11% of all branch misses | with the next two: **−20% branch misses**, −10% cycles |
| Move generator **templated on the side to move**, capture flag computed without a branch | |
| Threat tier of a square computed without branches | |
| Illegal-position guard only at the root; the child's in-check status inherited from the parent's gives-check | **−2% instructions** |
| Quiet-move scoring **per node** (history rows, masks, node cache looked up once, not per move) | −2.6% instructions |
| Hybrid accumulator refresh, pawn refresh cache and "both perspectives together" enabled on AVX-512 as well (all were switched off in August after timed measurements on a laptop) | hybrid −1.6%, pawn cache −1.4% instructions; perspectives **+0.80% NPS** |
| **Early prefetch** of the child's transposition-table and eval-cache entries, from an estimated key at the start of make (Stockfish's `key_after` idea) | **+2.21% NPS** |
| **Prefetch of the child's correction-history entries** | **+2.04% NPS** |
| **Prefetch of the threat PSQT rows** before the accumulator update uses them | **+1.76% NPS** |
| One TT probe instead of two at the search/quiescence boundary | −0.2% instructions |

With the same compiler, 8.0 now runs **5,647 instructions per node against Stockfish 19's 6,087**, with
fewer branch misses (27.3 against 28.8). The NPS gains in the table come from the paired runs, and each
has its 95% interval in the internal notes; the first batches predate the paired tool and are given
in counter terms.

## 5. Tried and dropped

- **Lazy move translation for the NNUE mirror board**: convert the move only when the position is
  evaluated. No gain (+0.15% instructions): almost every move made reaches an evaluation anyway.
- **Sorting the quiet moves once** instead of selecting: fewer instructions, more branch misses.
- **Removing the second board.** The engine keeps its own board and a Stockfish-style mirror for the
  network. Measured, the duplicated bookkeeping is about 2–2.5% of the time; the threat-delta work
  (~3%) is needed either way and Stockfish pays it too. Merging the boards would change the move
  generation order (our squares run a8 → h1, Stockfish's a1 → h8), hence the tree, for at most ~2%.
- **Dropping the eval cache.** It is not a pure cache: it returns evaluations computed with the
  optimism of the iteration that stored them, and turning it off changes the tree by 19%. That makes
  it a playing-strength question for an SPRT, not a speed one.
- **A mobility input block** ("threats on empty squares": one feature per knight, bishop, rook and
  queen — oriented square × four mobility buckets, 2,048 inputs), meant as a graft on the 8.0 network.
  Measured on the Consilium with paired simultaneous runs (480 samples): **−16.2 % NPS**
  (95 % interval −17.1 / −15.3). Mobility changes with every move, so the block recomputes about 28
  attack sets per node before and after the move. On top of the MoE's −7.5 % it would have needed
  more than 15 Elo just to break even; the PassedPawns graft gave 7. Removed from the engine on
  29 September 2026 (bench unchanged: 273477 with `legio-septima`).

## 6. TT16: the one change that alters the tree

The transposition table moved from 24-byte entries in 48-byte buckets — half of which straddled two
cache lines, so a probe could cost two misses — to **16-byte entries, four per 64-byte bucket**, one
cache line per probe, about 50% more entries per MB, and the bucket index computed with a high
multiply instead of a 64-bit division. The key check (48 bits) and the stored static eval share one
word protected by the same XOR as before. Because capacity and placement change, `bench` becomes
**240500** (240503 with the old table, still available with `-DTRIUMV_TT_LEGACY`).

**Game test**, TT16 against the old table, both PGO release builds, 10+0.1, Hash 16 (small on
purpose, where capacity matters): **+6.30 ± 5.06 Elo** on 5,365 games, LOS 99.3%, pentanomial
[42, 573, 1354, 662, 46]. Stopped with zero excluded. TT16 is the 8.0 table.

## 7. Result against 7.0

PGO release builds (clang, AVX-512) of the current source against the **official 7.0 binary**
(checksum verified), with the paired tool: 30 positions × 300,000 nodes, 20 physical cores in
parallel.

| comparison | NPS | 95% interval | faster in |
|---|---:|---|---:|
| **7.0 → 8.0, old table** (identical tree, bench 240503) | **+8.70%** | [+8.63%, +8.78%] | 2,400 / 2,400 |
| 8.0 old table → 8.0 TT16 | +2.60% | [+2.42%, +2.77%] | 1,743 / 2,400 |
| **7.0 → 8.0 TT16** | **+11.54%** | [+11.40%, +11.68%] | 5,690 / 5,760 |

The two steps multiply to the direct figure (1.087 × 1.026 = 1.115). Null tests (the same binary
against itself) gave +0.05% under load and −0.10% on an idle machine, so the tool resolves about
0.1%. The +8.7% is speed and nothing else: same moves, same nodes, same tree as 7.0.

**In games**, 8.0 (TT16) against the official 7.0 binary, both PGO release AVX-512, 1 thread:

| TC | hash | games | W / D / L | pentanomial | Elo | SPRT |
|---|---:|---:|---|---|---:|---|
| 12+0.12 | 128 MB | 4,664 | 1,135 / 2,595 / 934 | [26, 478, 1131, 659, 34] | **+14.71 ± 5.41** | `[0, 3]` H1 accepted |
| **60+0.6** | 256 MB | 5,422 | 1,247 / 3,110 / 1,065 | [7, 549, 1421, 717, 14] | **+11.68 ± 4.61** | `[0, 3]` H1 accepted |

UHO 2024 (+0.85/+0.94). The 12+0.12 row is the speed work plus TT16; the 60+0.6 row is the release
candidate `rc2`, which also has the two features switched off in section 8 (bench 273477). Speed is
worth most at short time controls; at 60+0.6 the gain holds.

## 8. Ablations: switching off instead of adding

Porting search ideas from other engines kept coming back flat, so we tried the opposite: switch off,
one at a time, features that had been accepted on weak evidence or validated with older networks, with
a simplification SPRT (`[-1.75, 0.25]`, 10+0.1). A feature stays if removing it costs Elo.

| feature | why it was suspect | without it | outcome |
|---|---|---:|---|
| `QuietOffense` (wall-pawn penalty) | accepted on a trend in July, never closed | **+1.21 ± 2.12** (30,440 games) | switched off |
| `CorrHistMajor` | major-piece key redundant with the non-pawn key (Coda) | +0.32 ± 3.51 (11,028) | kept |
| `TTEvalImprove` | +4.55 on 1,758 games, never closed | −17.0 ± 16.2 (660) | kept |
| `ContHistPrune` | adopted "pending a longer-TC test" after an ultrabullet run | **+5.94 ± 3.98** (8,957) | switched off |
| `ContHist36` | continuation history 3/6 plies, bundled in June with older networks | +0.93 ± 3.88 (9,303) | kept |
| `ThreatOrdering` | idem | −10.8 ± 12.4 (972) | kept |
| `CheckOrdering` | idem | −15.6 ± 12.6 (792) | kept |
| `PriorBonus` | validated with older networks | −21.5 ± 17.8 (393) | kept |
| `LowPlyHistory` | idem | +0.81 ± 3.52 (11,201) | kept |
| `CorrHistMajor` + `ContHist36` together | the two light leans combined | −1.02 ± 5.15 (5,132) | kept |

The code of switched-off features stays; only the default changes. Also measured and left off: a
correction history keyed by the last move in context (Coda, Cinder), −7.6 ± 7.8 at 20+0.2. Coda's
time-management fix for rising evaluations does not apply here: our eval-stability factor is already
symmetric.

## 9. The 8.0 network: Consilium, against 7.0

After the speed work (sections 4–7) and the ablations (section 8), the third step of 8.0 is a new
network: **Consilium**, the `legio-septima` architecture with the king-relative block split into four
experts by game phase. Design, data, training and every intermediate measurement are in
[NETWORKS.md](NETWORKS.md#consilium--the-triumviratus-80-network).

**Release result** (30 September 2026): final network, network-dependent SPSA baked into the code,
release build.

| engine | against | TC | games | pentanomial | Elo |
|---|---|---|---:|---|---:|
| **8.0 release**, PGO, MoE F4 (average of the last 5 epochs, permuted), SPSA MOE1 baked | **official 7.0 binary** (AVX-512) | 15+0.15 | 2,000 | [10, 180, 471, 321, 18] | **+27.3 ± 8.3** |

1 thread, 64 MB hash, UHO 2024 (+0.85/+0.94), LOS 100%. The 8.0 side is the build we would ship: tuning
options frozen, no option set by the test. `NullThreatExt` is **off** (its SPRT, +2.75 ± 5.5, never
confirmed it). The two sockets of the test machine, measured as separate halves, agree: +26.8 ± 11.7 and
+27.9 ± 11.8.

How the pieces were chosen, all on the same day:

| step | test | TC | games | Elo |
|---|---|---|---:|---:|
| final network: F4 average-and-permute against the end of F3 (same engine, same settings) | MoE vs MoE | 8+0.08 | 686 | +19.3 ± 13.9 |
| SPSA MOE1 (25 network-dependent parameters: per-phase eval scale, eval blend, pruning margins), vector at 4,363 iterations against its starting point, same network | 8.0 vs 8.0 | 10+0.1 | 928 | +8.6 ± 11.5 |
| the SPSA vector was then baked at iteration 5,410; release bench **362367** equals the dev build with the 25 options set by hand | | | | |

**Second measurement lesson: NUMA.** The two Xeons are two NUMA nodes, and Windows showed every engine
of a match as able to run on both. One socket also had **no memory of its own** (all six DIMMs sat on
the other CPU), so any engine scheduled there read its network through the socket link. We moved two
DIMMs to the second CPU (4 + 2, both memory controllers balanced) and now pin each fastchess instance,
and the engines it starts, to one socket (`start /NODE`). With pinning the saturation problem described
below largely goes away: 8.0 against 7.0 at 10+0.1 gave +20 ± 11 at concurrency 74, in line with the
concurrency-38 results, and the release test above ran at 76.

**First release-level number** (29 September 2026, provisional: the
network is the end of the F3 fine-tune, epoch 79, not yet the final one):

| engine | against | TC | games | pentanomial | Elo |
|---|---|---|---:|---|---:|
| 8.0 MoE, PGO, network F3 ep. 79 | **official 7.0 binary** (AVX-512, checksum verified) | 20+0.2 | 2,000 | [4, 203, 467, 314, 12] | **+22.1 ± 8.1** |

1 thread, 64 MB hash, UHO 2024 (+0.85/+0.94), LOS 100%. The 8.0 side carries everything so far: the
speed work and TT16, the two ablations, `NullThreatExt=300` (its SPRT gave +2.75 ± 5.5 on 7,070 games,
not conclusive; it is off in the release above), the new network, and a first, partial SPSA of the
25 search and evaluation parameters that depend on the network's scale (1,691 of 6,000 iterations,
+6 ± 10 on its own). The final network and the full SPSA are still to come.

**A measurement lesson from the same day: do not saturate hyperthreads.** The test machine has 40
physical cores (2× Xeon Gold 6138, 80 threads). For weeks the network matches ran 75 games at a time,
so most engines shared a physical core, and its L1/L2 cache, with another. The MoE's first layer is
almost twice the size of `legio-septima`'s (~158 M weights against ~89 M) and suffers more from a
shared cache. Same network, same settings, only the concurrency changed:

| MoE (F3, + SPSA) against `legio-septima` 8.0 | concurrency | games | Elo |
|---|---:|---:|---:|
| 30+0.3 | 75 | 1,260 | +0.8 ± 10.1 |
| 90+0.9 | 75 | 720 | +14.5 ± 12.6 |
| **12+0.12** | **38** | 922 | **+23.4 ± 12.0** |

**The network step on its own** (network plus its per-phase eval-scale calibration, which belongs to
the network upgrade; same 8.0 search on both sides, no SPSA): the five checkpoints tested against
`legio-septima` after the lambda fix (epochs 321–368, 30+0.3) pooled give **+6.3 ± 4.4 Elo** over 6,738
games, pentanomial [30, 758, 1675, 871, 35]. Those matches ran at concurrency 75, so this is a lower
bound: see the table above for what the hyperthread saturation hides.

At 75 the MoE looked flat at short time controls and positive only at long ones; with one engine per
physical core the gain shows at every time control. From now on network tests run at most 38 games at
a time on this machine. Against Viridithas 20 under the same conditions (12+0.12, concurrency 38) the
8.0 MoE scored **+7.3 ± 15.1** (570 games, LOS 83%).

## 10. Endgame depth study

In CCRL games Stockfish reaches depth 60–80 in endgames within a minute, while 7.0 stays much shallower.
Measured on 60 real endgames (10 pieces) at equal node budgets: Stockfish 19 reaches depth 40.6, we
reach 33.5. Up to depth 20 the two trees grow alike; above it ours keeps branching (1.25 per ply
against 1.15). Instrumented builds of both engines found three differences at depth ≥ 12:

| | Triumviratus | Stockfish 19 |
|---|---:|---:|
| mean LMR reduction | 3.6 ply | 6.5 ply |
| TT entries whose bound fits the window | 53% | 66% |
| successful null moves | 60% | 72% |

Our LMR table was tuned by SPSA at short time controls, where searches never pass depth 15, so its
slope beyond that was never seen by the tuner. But none of the differences transplants on its own:
Stockfish's reduction base goes 4 ply deeper in endgames and loses 33 Elo; a steeper slope only above
depth 12 is neutral at 40+0.4; Stockfish's TT replacement rule and null-move conditions do not shorten
our tree. Stockfish's tree shape comes from all its parameters tuned together, at long time controls.
The options are in the code (off) as axes for a long-time-control tuning of the LMR and null-move
block, after the next network.

## 11. Status

- Every change in section 4 is in `source/` and enabled on all targets (AVX2, AVX-512, VNNI, ICL,
  `-intel`).
- **Done:** TT16 adopted (+6.3 ± 5.1); 8.0 against 7.0: +14.7 ± 5.4 at 12+0.12 and +11.7 ± 4.6 at 60+0.6,
  both SPRTs passed. Ablations A1 and A4 switched off two features; bench is now **273477**.
- **Tried:** a correction history keyed by the last move in context (hash of parent XOR hash of
  node, as in Coda and Cinder): −7.6 ± 7.8 on 2,069 games at 20+0.2. Left in the code, switched off.
- **Cleanup:** 14 finished compile-time switches removed (the old table, the speed-work oracles,
  prefetch and permutation experiments that were measured and rejected): about 2,000 lines fewer.
  Same tree, checked: bench 240500 and identical node counts on the 50 test positions. Search
  options that are switched off stay in the code.
- **Ablation campaign closed:** nine tests, two features switched off (about +7 Elo together), the rest
  needed or neutral.
- **Network:** instead of a wider L1, the 8.0 network is a **mixture of experts** on the king-relative
  block (Consilium, build option `TRIUMV_PSQ_PHASES=4`), in training since 28 September. Design,
  data, recipe and every measurement are in [NETWORKS.md](NETWORKS.md#consilium--the-triumviratus-80-network).
  The mobility block planned as a graft was measured and dropped (section 5).
- Found on the way: the engine did not support Chess960 FENs (it accepted the castling rights and then
  generated castling moves from the wrong squares). Now supported: see section 14.

**Tools** (`build/`): `nps_pair.py` (paired simultaneous NPS), `cpu_topology.py` (hyperthread
siblings), `node_identity.py` (same tree check), `uci_workload.py` (common workload for any UCI engine).
`uci_stress.py` (UCI robustness), `ccrl_analyze.py` / `ccrl_report.py` / `ccrl_deep.py` (CCRL game analysis),
`perft960.py` (Chess960 perft suite, through `perft` or the per-thread `tdperft`, see section 14).

## 12. Correctness audit, SMP and the CCRL games

**Correctness.** Checked with tests rather than by reading the code:
- per-thread perft on 171 positions (482M nodes, hash and keys checked at every node): 0 errors;
- incremental NNUE checked against a full refresh during search: 0 differences;
- UBSan on bench, perft and Syzygy endgames;
- 27 UCI stress scenarios (`build/uci_stress.py`);
- 400 games at 4 threads with Hash 256 and Syzygy: no crashes, time losses or illegal moves.

Eight fixes, none of which changes the 1-thread tree (bench still 273477):
- correction-history decay computed in 64 bits (a signed overflow in tablebase endgames);
- a clock of exactly −1 no longer means "no clock";
- `bestmove` and `readyok` printed under the output mutex;
- thread 0's PV reset before the helpers start;
- repeated spaces and a trailing `\r` accepted in UCI input;
- an en-passant square that does not match the side to move is ignored instead of crashing;
- a 64 KB input buffer;
- an 8 MB thread stack on Windows.

A second round (27/09), again with the same 1-thread tree:
- a proven mate now wins over depth when the result is chosen among threads;
- `go nodes` counts the nodes of all threads;
- `go infinite` waits for `stop`;
- castling rights that do not match the board are dropped;
- `UCI_ShowWDL`, `SyzygyProbeDepth` and `SyzygyProbeLimit` are honoured by the release build.

Still open: ponder is not implemented.

**SMP.** With threads pinned one per core, NPS scaling matches Stockfish. On CCRL 40/15, the step
from 1 to 4 CPUs is worth +27 (6.0) and +33 (5.0) for us, against +20..+23 for Stockfish and
+19..+34 for the other top engines, so there is no SMP defect to fix. Two SPRTs at 4 threads came
out neutral, and both options are off:
- `ThreadVoting`: −3.5 ± 8.7;
- `OptPerThread` (optimism per thread, as in SF): +0.9 ± 8.6.

**CCRL Blitz games of 7.0** (750 games, +42 =693 −15), re-analysed with Stockfish 19 at 300k nodes
per position (`build/ccrl_analyze.py`, `build/ccrl_report.py`):
- we lose less per move than our opponents in every phase;
- no draw was thrown from a clear advantage;
- the losses are slow middlegame drifts.

A deeper pass at 3M nodes (`build/ccrl_deep.py`) found 23 real mistakes. 7.0 avoids 17 of them with
10 s per move and 20 with 60 s, so they are horizon errors: more depth fixes them.

## 13. Next: a long time-control SPSA

Stockfish still tunes its search with SPSA at long time control after every network. Our last
search-wide SPSA ran at short time control, on older networks. The run is prepared in
`build/spsa_ltc/`:
- 46 continuous search parameters (pruning margins, the full fine-grained LMR, singular margins,
  history weights);
- starting values taken from the compiled source, not from the UCI defaults;
- 40+0.4, about 88k games, about 48 hours;
- the verdict comes only from an SPRT of the final vector against the defaults at the same time control.

Before the run, a simulation of the tuner's exact update rule (`sim_server_math.py`) showed that the
learning rate we used in September (0.06–0.08) loses Elo when the landscape is flat: noise moves the
parameters more than the gradient does. The run uses 0.02, with the perturbation at 20% of each range.

Three ideas taken from reading Coda 0.9.4's source join the plan, each as an option that is off by
default and gets an SPRT at 40+0.4 before the SPSA:
- **pruning that relaxes as the whole search gets deeper** (it depends on the root depth, not on the
  depth of the node);
- **TT cutoffs from entries one ply short**, with a score margin;
- **TT scores damped toward beta** at non-PV cutoffs.

A scan of the other reference engines (Stockfish, Reckless, PlentyChess, Caissa, Integral,
Stormphrax, Viridithas, Berserk) for anything that depends on the root depth adds two tests, each
shared by four engines:
- **the TT move on the principal variation never drops into quiescence** (new option `PvTTMinDepth`);
- **singular extensions only while ply < 2 × root depth** (`SingularPlyGuard`: already in the code,
  never measured on its own).

Bolted on one at a time, at 10+0.1, none of them holds up:

| test | result |
|---|---|
| near-miss alone | +2.4 ± 7.2 (4,216 games) |
| near-miss + damping | −1.6 ± 18.8 (634 games) |
| TT-move rule + ply guard | −5.7 ± 15.4 (918 games) |
| ply guard alone | +0.7 ± 8.8 (about 2,700 games) |

The surrounding parameters were tuned without these ideas. For example, the TT cutoff asks one extra
ply for fail-highs, while the near-miss accepts one ply less. So the continuous ones go into the SPSA
**switched on**, to be retuned together with their neighbours, and each has a continuous path back to
"off". The verdict is still the SPRT of the tuned vector against the defaults. The SPSA runs at
20+0.2, without the root-depth terms, which only act at long time control.

## 14. Chess960 (Fischer Random Chess)

**8.0 is the first version of Triumviratus to support Chess960**, through the standard `UCI_Chess960`
option.
- **Positions.** It reads both X-FEN (`KQkq` means the outermost rook on that side) and Shredder-FEN
  (`HAha`, the rook files).
- **Moves.** Internally a castling move is still "king to g1/c1" with a castling flag. The rook's
  starting square now comes from one table instead of eight hard-coded switches. In UCI, castling is
  written as the king capturing its own rook (`e1h1`).
- **The Chess960 edge cases:** the king does not move at all, or lands on its own rook's square.
  Two places had to change for them:
  - the incremental occupancy update now XORs the squares instead of ORing them;
  - the piece-on-square table clears both origins before filling both destinations.

  The network update already removed both pieces before placing them, as Stockfish does.

Verification:
- **Standard chess is untouched.** Bench 273477 with the option off and on, and the same perft counts.
- **Official FRC perft suite** (`frcperftsuite.epd`, 960 positions), with zero errors:
  - the main move generator: every position at depth 4, 200 at depth 5;
  - the per-thread search board through `tdperft`: every position at depth 3, 150 at depth 4.
- **Network.** Incremental updates match a full refresh on 40 Chess960 searches.
- **Games.** 8 test games against Stockfish 19: no crash and no illegal move. The engine castled
  Chess960-style and correctly read Stockfish's castling moves.
- **No speed cost.** Full PGO release builds from before and after the Chess960 work, run as paired
  simultaneous NPS: the new one is **+0.57%** [+0.53, +0.62] on standard positions (null test +0.00%).
  `UCI_Chess960` is therefore exposed in the release build too.

**Credit: `tdperft`.** Most of this verification rests on `tdperft`, the per-thread perft written for
the correctness audit (section 12). The ordinary perft only exercises the main board, which the
search never uses. `tdperft` runs the search's own move generator, make and unmake. At every node it
recomputes the hash, pawn, non-pawn and minor/major keys, the occupancy and the piece-on-square table
from scratch and compares them with the incremental ones. It also re-checks every move of the parent
position against the pseudo-legality test used for TT and killer moves. It found nothing in the audit
(171 positions, 482M nodes) and nothing in Chess960. It is now a permanent development command
(`tdperft N`), and `build/perft960.py` runs the FRC suite through either perft.

## 15. Code layout

`threads.cpp` had grown to 12,000 lines. It is now split into parts under `source/search/`
(parameters, frozen constants, make/unmake, move generation, ordering, qsearch, negamax, iterative
deepening, SMP, and `tdperft`), included in order by `threads.cpp`.

It stays a single compilation unit, for two reasons: the hot-path `static inline` functions have to
be compiled together with their callers, and the frozen-parameter `#define`s apply to all the code
that follows them. The split has its own commit, and it is a pure move: compiled before and after, the
object file is identical byte for byte.

