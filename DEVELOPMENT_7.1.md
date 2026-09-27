<div align="center">

<img src="logo.png" alt="Triumviratus" width="200">

# Triumviratus 7.1 — development log

**Started as a speed project.** Same network as 7.0 (`legio-septima`) · the code around it made faster · where it ends up is still open

**by Francesco Torsello**

<sub>in collaboration with Maurizio Platino</sub>

</div>

---

<div align="center">

[Why speed](#1-why-speed) · [How it is measured](#2-how-it-is-measured) ·
[Where we started](#3-where-we-started) · [What changed](#4-what-changed-identical-tree) ·
[Tried and dropped](#5-tried-and-dropped) · [TT16](#6-tt16-the-one-change-that-alters-the-tree) ·
[Result](#7-result-against-70) · [Ablations](#8-ablations-switching-off-instead-of-adding) · [Endgame depth](#9-endgame-depth-study) · [Status](#10-status) · [7.0 log](DEVELOPMENT_7.0.md)

</div>

---

> [!NOTE]
> **Work in progress.** `source/` now holds the 7.1 development code; the 7.0 release is the tag
> `v7.0`. Every change in section 4 leaves the search tree **bit-for-bit identical** (same `bench`,
> same node counts on 50 positions at depth 15), so it can only change speed, never play. Against the
> official 7.0 binary, the same tree now runs **+8.7% faster**, and **+11.5%** with the new
> transposition table (section 6). In games, 7.1 beats the 7.0 release by **+14.7 ± 5.4 Elo** at
> 12+0.12 and, with two search features switched off after ablation tests (section 8), by
> **+11.7 ± 4.6 Elo at 60+0.6** (section 7).

---

## 1. Why speed

7.1 started with an audit of the search against the engines released in the last few months:
Stockfish 19, Reckless, PlentyChess 8, Integral 8, Caissa 2.0, Stormphrax 8 and Viridithas 20.
Porting their search ideas did **not** pay here. Three SPRTs in a row came back flat or negative:
dropping null-move pruning inside the singular search (−23 ± 19, stopped early), fail-high score
blending (−0.6 ± 7.3 on 2,502 games) and a bundle of five small ports (−4.4 ± 7.1 on 4,212 games).
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
| **Triumviratus 7.1 at the start** | **6,768** | **8,728** | **37.7** |

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

With the same compiler, 7.1 now runs **5,647 instructions per node against Stockfish 19's 6,087**, with
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

## 6. TT16: the one change that alters the tree

The transposition table moved from 24-byte entries in 48-byte buckets — half of which straddled two
cache lines, so a probe could cost two misses — to **16-byte entries, four per 64-byte bucket**, one
cache line per probe, about 50% more entries per MB, and the bucket index computed with a high
multiply instead of a 64-bit division. The key check (48 bits) and the stored static eval share one
word protected by the same XOR as before. Because capacity and placement change, `bench` becomes
**240500** (240503 with the old table, still available with `-DTRIUMV_TT_LEGACY`).

**Game test**, TT16 against the old table, both PGO release builds, 10+0.1, Hash 16 (small on
purpose, where capacity matters): **+6.30 ± 5.06 Elo** on 5,365 games, LOS 99.3%, pentanomial
[42, 573, 1354, 662, 46]. Stopped with zero excluded. TT16 is the 7.1 table.

## 7. Result against 7.0

PGO release builds (clang, AVX-512) of the current source against the **official 7.0 binary**
(checksum verified), with the paired tool: 30 positions × 300,000 nodes, 20 physical cores in
parallel.

| comparison | NPS | 95% interval | faster in |
|---|---:|---|---:|
| **7.0 → 7.1, old table** (identical tree, bench 240503) | **+8.70%** | [+8.63%, +8.78%] | 2,400 / 2,400 |
| 7.1 old table → 7.1 TT16 | +2.60% | [+2.42%, +2.77%] | 1,743 / 2,400 |
| **7.0 → 7.1 TT16** | **+11.54%** | [+11.40%, +11.68%] | 5,690 / 5,760 |

The two steps multiply to the direct figure (1.087 × 1.026 = 1.115). Null tests (the same binary
against itself) gave +0.05% under load and −0.10% on an idle machine, so the tool resolves about
0.1%. The +8.7% is speed and nothing else: same moves, same nodes, same tree as 7.0.

**In games**, 7.1 (TT16) against the official 7.0 binary, both PGO release AVX-512, 1 thread:

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

## 9. Endgame depth study

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

## 10. Status

- Every change in section 4 is in `source/` and enabled on all targets (AVX2, AVX-512, VNNI, ICL,
  `-intel`).
- **Done:** TT16 adopted (+6.3 ± 5.1); 7.1 against 7.0: +14.7 ± 5.4 at 12+0.12 and +11.7 ± 4.6 at 60+0.6,
  both SPRTs passed. Ablations A1 and A4 switched off two features; bench is now **273477**.
- **Tried:** a correction history keyed by the last move in context (hash of parent XOR hash of
  node, as in Coda and Cinder): −7.6 ± 7.8 on 2,069 games at 20+0.2. Left in the code, switched off.
- **Cleanup:** 14 finished compile-time switches removed (the old table, the speed-work oracles,
  prefetch and permutation experiments that were measured and rejected): about 2,000 lines fewer.
  Same tree, checked: bench 240500 and identical node counts on the 50 test positions. Search
  options that are switched off stay in the code.
- **Ablation campaign closed:** nine tests, two features switched off (about +7 Elo together), the rest
  needed or neutral.
- **Next:** the next network (larger L1), with an L1 penalty on the feature-transformer activations in
  the recipe (one of the recipe changes behind Coda 0.9.4's gain).
- Found on the way: the engine did not support Chess960 FENs (it accepted the castling rights and then
  generated castling moves from the wrong squares). Now supported: see section 13.

**Tools** (`build/`): `nps_pair.py` (paired simultaneous NPS), `cpu_topology.py` (hyperthread
siblings), `node_identity.py` (same tree check), `uci_workload.py` (common workload for any UCI engine).
`uci_stress.py` (UCI robustness), `ccrl_analyze.py` / `ccrl_report.py` / `ccrl_deep.py` (CCRL game analysis),
`perft960.py` (Chess960 perft suite, through `perft` or the per-thread `tdperft`, see section 13).

## 11. Correctness audit, SMP and the CCRL games

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

## 12. Next: a long time-control SPSA

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

## 13. Chess960 (Fischer Random Chess)

**7.1 is the first version of Triumviratus to support Chess960**, through the standard `UCI_Chess960`
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

**Credit: `tdperft`.** Most of this verification rests on `tdperft`, the per-thread perft written for
the correctness audit (section 11). The ordinary perft only exercises the main board, which the
search never uses. `tdperft` runs the search's own move generator, make and unmake. At every node it
recomputes the hash, pawn, non-pawn and minor/major keys, the occupancy and the piece-on-square table
from scratch and compares them with the incremental ones. It also re-checks every move of the parent
position against the pseudo-legality test used for TT and killer moves. It found nothing in the audit
(171 positions, 482M nodes) and nothing in Chess960. It is now a permanent development command
(`tdperft N`), and `build/perft960.py` runs the FRC suite through either perft.

## 14. Code layout

`threads.cpp` had grown to 12,000 lines. It is now split into parts under `source/search/`
(parameters, frozen constants, make/unmake, move generation, ordering, qsearch, negamax, iterative
deepening, SMP, and `tdperft`), included in order by `threads.cpp`.

It stays a single compilation unit, for two reasons: the hot-path `static inline` functions have to
be compiled together with their callers, and the frozen-parameter `#define`s apply to all the code
that follows them. The split has its own commit, and it is a pure move: compiled before and after, the
object file is identical byte for byte.

