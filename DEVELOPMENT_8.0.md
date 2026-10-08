<div align="center">

<img src="logo.png" alt="Triumviratus" width="200">

# Triumviratus 8.0 — development log

**Started as a speed project.** The code around the network made faster · ablations · then a new network, Consilium · still in progress

**by Francesco Torsello**

<sub>in collaboration with Maurizio Platino</sub>

</div>

---

<div align="center">

[The path so far](#the-path-so-far) · [Where things stand](#where-things-stand-6-october-2026) ·
[Why speed](#1-why-speed) · [How it is measured](#2-how-it-is-measured) ·
[Where we started](#3-where-we-started) · [What changed](#4-what-changed-identical-tree) ·
[Tried and dropped](#5-tried-and-dropped) · [TT16](#6-tt16-the-one-change-that-alters-the-tree) ·
[Result](#7-result-against-70) · [Ablations](#8-ablations-switching-off-instead-of-adding) · [Consilium](#9-the-80-network-consilium-against-70) · [Endgame depth](#10-endgame-depth-study) · [Status on 1 October](#11-status-on-1-october-and-the-tools) ·
[Correctness](#12-correctness-audit-smp-and-the-ccrl-games) · [SPSA M20](#13-a-search-wide-spsa-on-the-old-search-1-october-2026) · [Chess960](#14-chess960-fischer-random-chess) · [Code layout](#15-code-layout) ·
[Gap to Stockfish](#16-where-the-gap-to-stockfish-comes-from-and-a-pruning-rework-3-october-2026) · [Search restructured](#17-the-search-restructured-4-october-2026) ·
[Our own ideas](#18-our-own-search-ideas-tested-one-at-a-time-5-october-2026) · [Long-TC SPSA](#19-a-long-time-control-spsa-of-what-rw1-could-not-see-5-october-2026) ·
[Audits and speed](#20-audits-clean-up-and-more-speed-5-october-2026) · [First idea adopted](#21-the-first-idea-adopted-a-hash-move-extension-at-low-depth-5-october-2026) ·
[Guard, stack probe, expected reply](#22-a-guard-for-ldse-the-stack-probe-and-the-expected-reply-5-october-2026) · [Surprise rule, refresh path](#23-the-guard-adopted-the-surprise-rule-and-the-refresh-path-5-october-2026-evening) ·
[Against Stockfish 19](#24-against-stockfish-19-and-the-printed-scale-6-october-2026) · [Depth, pins, corrections](#25-ideas-that-need-depth-pins-and-the-correction-history-6-october-2026) ·
[Opponent's plan, progress check](#26-the-opponents-plan-a-combined-spsa-and-a-progress-check-6-october-2026-evening) ·
[One lost game, examined](#27-one-lost-game-examined-and-the-clock-6-october-2026-night) ·
[All ideas tested](#appendix-every-search-idea-tested-since-the-restructured-search) · [7.0 log](archive/DEVELOPMENT_7.0.md)

</div>

---

> [!NOTE]
> **Work in progress.** `source/` holds the 8.0 development code; the 7.0 release is the tag `v7.0`, and
> the current 8.0 prerelease is the tag `v8.0`. The sections follow the order in which the work was done,
> each step starting from what the previous one found. Current `bench`: **430151**.

## The path so far

8.0 started from the 7.0 release (CCRL Blitz 8th among single-CPU engines) with one question: where does our time
go, compared with Stockfish? Each step answered the question the previous one left open.

| step | sections | what | measured against the step before | bench after |
|---|---|---|---|---:|
| **Speed**, same tree | 1–7 | the code around the network made faster, a new transposition table (TT16) | **+11.5% NPS** with an identical tree; +14.7 ± 5.4 Elo against 7.0 at 12+0.12 | 240500 |
| **Ablations** | 8 | switch off what was accepted on weak evidence | two features off; +11.7 ± 4.6 against 7.0 at 60+0.6 | 273477 |
| **A new network, Consilium** | 9, 13 | four phase experts on the king-relative block, network SPSA, then a 45-parameter search SPSA | **+27.3 ± 8.3** against 7.0 at 15+0.15 (+27.6 ± 7.0 with the search SPSA) | 402358 |
| **The gap to Stockfish** | 10, 16 | why we need more nodes for the same depth: selectivity, not evaluation; a pruning rework | +6.3 ± 6.0 at 20+0.2 | 337035 |
| **The search restructured** | 17 | Stockfish 19's search structure, our own ideas kept, complete SPSA re-tune on Consilium | **+85.8 ± 12.8** at 20+0.2 | 141196 |
| **Our own ideas, one at a time** | 18–23 | thirteen ideas built on what is specific to Triumviratus, audits, more speed; first adoptions | LDSE +4.5 ± 3.3, its guard +6.3 ± 7.9, the surprise rule +6.2 ± 6.8 (40+0.4) | 308883 |
| **Ideas that need depth, and the corrections** | 24–25 | a match against Stockfish 19, the printed scale, then the adopted SPRTs | quiescence hash depth +3.4 ± 2.6, pins in SEE +4.5 ± 6.8, two consistency fixes +1.6 ± 2.6, per-expert corrections +3.2 ± 3.8 | **309067** |
| **Progress check** | 26 | more continuation corrections (none adopted), a combined SPSA prepared, today's build against the 4 October prerelease and against Stockfish 19 | **+17.2 ± 6.8** at 10+0.1 for everything since 4 October; **50.0%** against Stockfish 19 at 133+1 (320 games) | 309067 |
| **The TT cutoff damping adopted** | 27 | the damping of section 18 (an idea from Coda) retried twice on the current code | +2.2 ± 3.2 over 11,774 games in three SPRTs | **430151** |
| **Search speed** | 27 | eight patches that only remove work, same tree | −1.75% cycles per node in the middlegame (all nine with AVX-512 gathers: −1.5% to −1.8% in endgames) | 430151 |

The direction, in short: first make the same search faster, then give it a better network, then find why it needed
more nodes than Stockfish and rebuild its structure, and now add small measured ideas on top of it. Every Elo figure
is an SPRT or a match against the step before, on the same machine, with its 95% interval. Since the 4 October
prerelease the speed work alone is about **−10.5% cycles per node** with an identical tree (sections 17, 20, 22, 23,
27).

## Where things stand (6 October 2026)

- **Engine:** the restructured search with the RW1 parameters (section 17), the Consilium network, the ideas adopted
  in sections 21–25, two consistency fixes and the TT cutoff damping (section 27). Bench **430151**. The prerelease
  builds on the tag `v8.0` are those of 6 October evening (bench 309067), before the damping: **+17.2 ± 6.8** at 10+0.1 against the 4 October prerelease
  (section 26).
- **Against Stockfish 19** at 133+1 (section 26): **+5 =310 −5 over 320, 50.0%** with this build; at 30+0.3 on
  random openings −4.4 ± 7.1 over 395 (section 27).
- **Running:** a time-management SPRT at 40+0.4 (section 27), a search speed study and a measurement of the time
  manager's instability signal.
- **Open:** the combined SPSA CORR1 (section 26); LDSE at 40+0.4; contempt in a gauntlet; large pages, which
  the test machine does not grant, so Triumviratus and Stockfish both run on 4 KB pages there; the shape of the next
  network.
- **Test rules,** as they evolved: one idea at a time on the same binary; at least 20,000 games or a clear verdict
  (section 21); search ideas that act deep in the tree at 15+0.15, the others at 10–12 s (section 25); an idea meant
  to gain Elo that comes out flat is analysed before it is closed (section 25).

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
| the same, plus the 45-parameter search SPSA M20 baked (1 October, section 13) | **official 7.0 binary** (AVX-512) | 15+0.15 | 2,760 | [10, 254, 644, 451, 21] | **+27.6 ± 7.0** |

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

## 11. Status on 1 October, and the tools

The current status is at the top of this log. On 1 October, before the restructured search: TT16 adopted
(+6.3 ± 5.1), 8.0 against 7.0 +14.7 ± 5.4 at 12+0.12 and +11.7 ± 4.6 at 60+0.6, two features switched off by
ablation, about 2,000 lines of finished switches removed, Consilium in training, the M20 SPSA (section 13), Chess960
support (section 14). The sections that follow pick up from there.

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

**Ponder** (added 1 October): the `Ponder` option, `go ponder`, `ponderhit`, and `bestmove … ponder …`.
- Time follows Stockfish: the budget is counted from `go ponder`. If it is already spent at
  `ponderhit`, the engine plays at once with its last completed iteration.
- The move to ponder comes from the PV of the last completed iteration, so it is there even when the
  search is stopped mid-iteration.
- Bench is unchanged.

Checked in 50 games with ponder on for both sides, refereed by python-chess (which drives `go ponder`,
`ponderhit` and `stop` like a GUI):

| TC | games | moves | ponderhits |
|---|---:|---:|---:|
| 5+0.05 | 30 | 4,044 | 59.5% |
| 1+0.01 | 20 | 2,723 | 59.8% |

In those games there were no time losses, no illegal moves and no illegal ponder moves. After a
ponderhit the move takes less time than a normal move (median 57 against 65 ms at 5+0.05).

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

## 13. A search-wide SPSA on the old search (1 October 2026)

Superseded by the restructured search of section 17, so only the outcome is kept. A simulation of the tuner's
update rule showed that the learning rate used in September (0.06–0.08) loses Elo on a flat landscape: noise
moves the parameters more than the gradient does, so the run used 0.02. Five ideas from Coda and from the
other reference engines (pruning that relaxes with the root depth, TT cutoffs from entries one ply short,
TT scores damped toward beta, the PV TT move never dropping into quiescence, a ply guard on singular
extensions) were first tested one at a time at 10+0.1 and none held up alone (best: near-miss +2.4 ± 7.2 on
4,216 games), so the continuous ones entered the SPSA switched on. The run, **M20**, had 45 parameters at
20+0.2 on Consilium; the vector, the mean of the last 1,100 of 13,104 iterations, passed its gate at
**+9.9 ± 6.6** over 2,978 games and was baked (bench 402358). Against the official 7.0 the release with M20
gave +27.6 ± 7.0, the same as without it (+27.3 ± 8.3): the gain did not show against 7.0.

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

## 16. Where the gap to Stockfish comes from, and a pruning rework (3 October 2026)

To find weaknesses in Triumviratus' search, the Stockfish code was studied and some of its logic was implemented
from there (Stockfish is GPLv3, like Triumviratus).

**Decomposing the gap.** Against Stockfish 19, single thread:

| Test | Result |
|---|---|
| Speed, one core, 30 positions | SF searches 1.24× our nodes per second; in 3 s it reaches depth 23, we reach 20 |
| Fixed depth 6 / 8 | **+35 / +25** Elo for us |
| Fixed depth 10 / 14 | −22 / −67 |
| Fixed nodes (300k per move) | −98 |
| Time (20+0.2) | −127 |
| Nodes needed to complete the same depth | 1.5–1.7× SF's |

So the network is not the problem: at low depth, where evaluation dominates, we are ahead. Speed explains
about 25–30 Elo. The rest is search selectivity: Stockfish turns each nominal ply into more real depth.

**Same counters in both engines.** `source/sstats.h` adds per-depth counters to the search (only with
`-DTRIUMV_SSTATS`; without it the code is identical). The same header went into a local copy of Stockfish 19,
and both engines searched the same 300 positions to depth 16. The main finding:

- at remaining depth 8, Stockfish's futility prunes about 720,000 quiet moves per 100 positions, ours about
  1,000; we play 7.9 quiet moves per node, Stockfish 2.9;
- the reason is the depth used to decide pruning. Ours came from our own reduction table, which reduces about
  half of Stockfish's, and was floored at 0; Stockfish's averages −1.5 because history pushes bad quiets below
  zero. Our history values are also capped at 7,000 against Stockfish's ~30,000, so they weigh less. And we did
  not prune at PV nodes;
- move ordering: near the leaves the first move cuts in 81% of fail-highs against Stockfish's 88%, and fewer
  of our shallow nodes have a TT move (33% against 48% at depth 2). None of our history weights moves this.

**The rework.** New options compute the pruning depth the Stockfish way (`PruneSFDepth`, `PruneNegDepth`),
use the corrected static eval for quiet futility (`FutStaticEval`), allow futility at PV nodes outside the
previous PV (`FutPVNodes`), and apply quiet SEE pruning at that depth (`SEELmrDepth`). Switched on together with
aggressive history weighting, they matched Stockfish's quiet moves per node but cost −35 ± 20 Elo: the rest of
the search was tuned for a wider tree. An SPSA over 29 coupled parameters (futility, pruning depth, capture
futility, SEE, LMP, LMR, RFP, the TT cut parameters, ProbCut, null move, singular double extensions, history size
and four depth thresholds), 15+0.15, stopped on a plateau at 3,509 iterations. It kept the new structure, brought
history back to its old weight, and moved pruning elsewhere: more RFP and ProbCut, more double extensions, null
move verification from depth 3 instead of 1.

**Result:** the tuned engine against the previous one, 20+0.2: **+6.3 ± 6.0 Elo over 3,366 games**. Baked as the
new defaults; bench **337035**.

**Open:** quiet futility still skips all remaining quiets once one is pruned (Stockfish decides move by move);
the history cap; TT moves at shallow nodes; the same SPSA at a longer time control, since short games bias it
toward pruning less.

**Cleanup.** 69 options that were measured and closed, or off for months with no plan, were retired: UCI line,
setter, frozen entry and declaration, then the branches they guarded. About 1,550 lines fewer in the search,
bench unchanged. Diagnostic tools stay (`sstats.h`, DataLog, TMLog, CutoffStats, SeeGEVerify, EvalOff), as do
levers still in use.


## 17. The search restructured (4 October 2026)

Section 16 left the engine needing 1.5–1.7× Stockfish's nodes to finish the same depth, and every single
rule changed on its own lost or stayed neutral (a different pruning structure −35, a larger history cap −41):
each rule only works tuned together with the others.

Triumviratus' search was restructured in October 2026. Nearly all of its structures were already in the engine, but
disordered and clogged by parameters and tests accumulated one at a time since version 5.0. After studying the searches of
Stockfish and Reckless, it was reorganised following the structure of Stockfish 19's search (GPLv3), keeping our own
ideas, with a complete SPSA re-tune on the new MoE network. It is Triumviratus' own code, with techniques of our own
such as passed-pawn pushes in endgames, and our own data structures, move generation, evaluation and network.

**What changed.** `source/search/` now holds:

- `01_params.inc`: the search parameters as one table (UCI spin options in development builds, constants in the
  release build); `02_state.inc` and `03_tables.inc`: search state and the statistics tables, some shared by all
  threads and sized by the thread count;
- `09_history.inc`: static evaluation in the units of the search margins, move statistics and the evaluation
  correction (pawn structure, minor pieces, non-pawn pieces of each side, and the pair of moves into the node);
- `10_order.inc` and `11_queue.inc`: move validation, repetitions, and a new move orderer;
- `12_quiesce.inc`, `13_search.inc`: quiescence and the main search, a node going through four phases (entry,
  evaluation and pruning before the moves, the move loop, learning at the end of the node);
- `14_deepen.inc`, `15_threads.inc`: iterative deepening with a new time manager, thread voting, bestmove.

Unchanged: the move generator, make/unmake (only the non-pawn keys re-wired), SEE (piece values now in the search
units), the transposition table, the network and the evaluation. The old search, about 3,700 lines of parameters
alone, is gone; the number of UCI options in development builds drops from about 380 to about 100.

**Checks.** Per-thread perft with key verification unchanged (4,865,609 at depth 5 from the start position,
4,085,603 at depth 4 from Kiwipete, no key mismatch). 74 fast games against the previous engine without illegal
moves or crashes. Nodes needed to complete a fixed depth on 100 UHO positions, against Stockfish 19 (median ratio
ours/SF): depth 8 **0.90**, depth 12 **1.11**, depth 16 **0.97**, down from 1.5–1.7. New bench **172833**.

**Result:** SPRT against the 8.0 of section 16, 10+0.1, bounds [0, 3]: **+61.6 ± 10.2 Elo over 1,390 games**,
LLR 3.85, accepted (H1).

**Our own techniques on top**, each an option tested on the same binary at 10+0.1:

| Technique | Result | Decision |
|---|---|---|
| Passed-pawn pushes in endgames: a push of a passed pawn to the 6th/7th rank with little material left is reduced less and never pruned (our network sees passed pawns through its `PassedPawns` block) | +4.4 ± 6.8 on the endgame book `endgame_12_18.epd`, no draw adjudication | kept, into the SPSA |
| King-shield pawns moved last in move ordering, up to the middlegame | −2.2 ± 16.6 | dropped |
| Correction history for rooks and queens | −2.1 ± 20.7 | dropped |
| TT entry one ply short accepted beyond a margin (from Coda) | +1.7 ± 12.6 | to be retried |

**Parameters re-tuned on our network.** SPSA "RW1": 51 parameters (evaluation scale, history bonuses, move ordering,
pruning margins, singular extensions, reductions, quiescence, correction weights, and the two of the passed-pawn
technique), 20+0.2, 5,481 iterations; the vector is the mean of the last 1,000. Kept out on purpose: time management,
depth thresholds and the shape of the reduction table, which a 20-second game cannot see. The evaluation scale
barely moved (1355 → 1347), confirming the calibration; the largest moves were capture futility −24%, singular
margin +24%, null-move base −14%, statistics divisor in reductions −14%, pawn-structure correction weight +13%.
Against the starting values at 10+0.1: **+9.7 ± 9.4 Elo**, stopped early and baked. New bench **141196**.

**The whole step at a longer time control.** The restructured, re-tuned search (bench 141196) against the 8.0 of
section 16, 20+0.2: **+85.8 ± 12.8 Elo over 694 games**, LLR 3.09, accepted (H1).

**Speed.** On one core (30 UHO positions, 3 s each) Stockfish 19 searched **1.18×** our nodes per second, down
from 1.24; both engines now count nodes the same way. A shortcut that decided legality once, before the move,
crashed the engine in about one game in seventy (a hash move that did not answer a check could be played) and was
withdrawn: the make-move test is back, tree unchanged.

**Speed work on the restructured search (4 October, night).** Hardware counters on the PGO build, six alternating
rounds, every change with an identical tree (bench 141196):

| change | cycles per node |
|---|---:|
| move ordering without branches (rank = number of larger unique keys), moves copied once instead of three times, three divisions out of the move loop, SEE reading the captured piece from the square table, enemy pawn attacks in bulk, make-move legality test only when in check | −1.70% |
| phase-change refreshes reuse the threat and pawn blocks: only the new phase's HalfKA rows are rebuilt | −0.55% |
| branch-free bookkeeping: one table-driven function for the three partial keys, discovered checks from aligned sliders, promotion type from a table, hash move removed once, four TT ways compared at once, two more prefetches, a reciprocal for the all-node reduction, branch-free passed-pawn test | −1.05% |
| PSQT rows summed inside the first accumulator tile, pawn generation without per-pawn branches | −0.6% |
| the PSQT prefetch loops switched off (no lead time left after the change above) | −1.32% |

Withdrawn: slider attacks from per-line tables (+0.55%), a single 32-register tile (+5.7%: the compiler spills),
the child's hash prefetch issued earlier (+0.03%). A per-function profile split our 6,700 cycles per node into about
3,500 for the network and 3,200 for the search, against Stockfish's 2,300 and 1,750: the search logic itself costs
about the same, the difference is move ordering, the TT, the SEE and make/unmake, much of it branch mispredictions
(46 per node against 29). Since the 4 October release the gains compound to about +4.8%; the gap to Stockfish 19 in
cycles per node went from 1.70× to 1.59×.

## 18. Our own search ideas, tested one at a time (5 October 2026)

A study of our search, written with Claude Fable 5.1 working as a research agent on the code and on the
measurements, proposed thirteen ideas built on what is specific to Triumviratus: the four experts of Consilium,
its passed-pawn block, the long time controls of the rating lists. All of them are in the code as options that are
off by default. Off, the search tree is unchanged (bench 141196), and the release build compiles them away. Each is
tested on its own against the defaults, on the same binary, SPRT [0, 3] at 10+0.1 unless noted. None of them uses
Stockfish's values: an idea that passes enters the next SPSA together with its neighbours.

| idea | what it does | result |
|---|---|---|
| Disagree | prune less where the network's two outputs, material and position, disagree | −12.6 ± 15.2 (690 games); with the opposite sign −8.5 ± 13.2 (818). Removed |
| PhaseEdge, margin | a capture that moves the game to another expert must clear an extra margin to cut | −2.5 ± 10.0 (1,262). Removed |
| PhaseEdge, improving | "improving" is not computed across evaluations of different experts | −14.1 ± 19.4 (296). Removed |
| PhaseEdge, no futility | a capture across an expert boundary is never pruned or reduced more | +1.3 ± 2.3 (24,292), neutral |
| Deep offsets | pruning and reduction offsets that grow with the root depth | no signal in the long-TC SPSA (section 19) |
| CorrPhase | the pawn and minor-piece correction tables kept separate for each expert | +2.8 ± 6.6 (3,202), lean |
| LateBranch | branches reached through a late move of the parent are reduced more | −5.1 ± 12.9 (892). Removed |
| Near-miss TT cutoffs (from Coda) | an entry one ply short of the required depth cuts beyond a margin | ported wrongly at first (two plies short on fail-high: −22.7 ± 19.5); corrected, fail-low only: −6.7 ± 10.6 (1,142) at 20+0.2. Removed |
| TT cutoff damping (from Coda) | a lower-bound cutoff value is pulled towards beta | +2.6 ± 6.8 (2,768) at 15+0.15, lean |
| Endgame: defensive replies | replies to an enemy passed-pawn push to the 6th/7th reduced less | −2.3 ± 4.6 (1,990). Removed |
| Endgame: null-move verification | null-move verification from a lower depth with little material | −6.7 ± 6.8 (728). Removed |
| Endgame: passer push in quiescence | a passed-pawn push to the 7th is searched as a tactical move | +1.1 ± 3.8 (3,370), lean |
| Endgame: transition extension | a capture that leaves one side with king and pawns only is extended | +1.4 ± 4.3 (3,030), lean (+6.2 at 1,118 games) |

The endgame ideas were tested on a book of endgames with 12–18 men, without draw adjudication. A depth ramp that
moves nine selectivity levers from the RW1 values to the LTC1 values (section 19) as the root depth grows gave
−0.9 ± 9.2 (1,486 games) at 20+0.2.

**The five leans together** (no futility at expert boundaries, CorrPhase, TT damping, passer push in quiescence,
transition extension) against the defaults, UHO 10+0.1: **−0.1 ± 6.7** (3,120 games). The single leans were mostly
noise, picked up by stopping each test while it looked good. Nothing was baked; the five stay in the code, off. The
ideas that lost were removed from the code (section 20).

Still to test: contempt, which aims at rating-list Elo and needs a gauntlet against weaker engines rather than
self-play, and time management with moves-to-go, as parameters for an SPSA of its own. Two more ideas need a hook in
the network and are not implemented: forcing the expert in a probe search, and smoothing the evaluation at expert
boundaries on the principal variation.

## 19. A long time-control SPSA of what RW1 could not see (5 October 2026)

RW1 (section 17) left out what a 20-second game cannot see. A second SPSA, LTC1, took those levers at 40+0.4:
- the shape of the reduction table;
- the depth limits of reverse futility and of null-move verification, and the null-move reduction;
- twelve reduction, pruning and singular parameters still at their starting values from Stockfish 19;
- four RW1 parameters that are sensitive to the time control;
- four of the root-depth offsets of section 18.

That is 25 parameters, on the development build with all the speed work above. Time management stayed out: in
self-play the side that spends more time wins, and the tuner would reward it.

At 1,929 of 4,000 iterations the trajectories had a clear direction: reduce and prune less at long time controls.
The reduction table got shorter, the depth thresholds lower, and the reductions without a TT move and at all-nodes
smaller. The root-depth offsets stayed at zero. The run was stopped there and its vector tested:

| vector | TC | games | Elo |
|---|---|---:|---:|
| extrapolated along the trend | 40+0.4 | 972 | −3.9 ± 10.5 |
| mean of the last 50 iterations | 30+0.3 | 606 | −4.6 ± 14.1 |

The extrapolation was a mistake: those values had never been played by the tuner. The actual vector showed no gain
either. At equal time, 20 s on 10 positions, it reached **0.6 ply less** on average (from −3 to +2 per position) and
needed 13–24% more time per ply at depths 20–23. No gain and a shallower search, so the RW1 values stay. The run is
kept and can be resumed.

## 20. Audits, clean-up and more speed (5 October 2026)

Four Claude Fable 5.1 agents worked in parallel on copies of the source, read-only or on their own builds, while
the tests of section 18 ran.

**Porting audit of our own techniques.** Most of our options were written for the old search and moved onto the
rewritten one, whose conventions differ (a TT cutoff from a fail-high entry needs one more ply of depth than a
fail-low one). The audit checked each option against the new rules and that "off" really leaves the tree unchanged.
Found and fixed:
- the near-miss TT cutoff accepted entries two plies short on the fail-high side (section 18);
- the passed-pawn push in quiescence was searched twice when it was already the hash move;
- contempt kept three "draw = 0" thresholds of the new search (upcoming repetition in the main search and in
  quiescence, the last-piece sacrifice) and scored tablebase draws as 0: with contempt on, alpha could drop. The
  thresholds are now the draw value of the side to move, and a tablebase draw is scored like any other draw.

With contempt off nothing changes (bench 141196).

**Audit of the rewritten core against Stockfish 19.** The behaviours were compared one by one: main search,
quiescence, iterative deepening and aspiration, move ordering, the seven histories, correction, singular extensions,
ProbCut, null move, reductions and re-searches, pruning, mates, draws and repetitions, SMP. No sign or depth error
was found in the logic. Seven small divergences sit in the adapters around it, and each will be tested behind an
option: the game ply ignores the move number of a FEN, so after a book position the first move gets about 30% less
time; a kept TT entry does not take the new move; SEE lets pinned pieces recapture; the quiescence TT depth is −1
instead of 0; the null move advances the fifty-move counter; two slightly different fifty-move fade formulas; a
maximum ply of 128.

**Clean-up.** The ideas of section 18 that lost were removed from the code: Disagree, PhaseEdge margin and
improving, LateBranch, defensive replies, endgame null-move verification, near-miss TT cutoffs, together with the
data that only they used (the network disagreement carried through the evaluation cache, a piece count in every
search frame). Bench unchanged: 141196, and 2,026,045 nodes on 30 positions at depth 14.

**New search ideas, implemented off.** Techniques that neither Stockfish 19 nor Triumviratus had, found in several
of the engines we study (Reckless, PlentyChess, Viridithas, Integral, Berserk, Caissa, Stormphrax), written in our
own code with our own values:

| option | idea |
|---|---|
| `HashFiftyStep` | the TT key carries a band of the fifty-move counter, above 40 plies |
| `RfpOppCapture`, `NmpOppCapture` | no "improving" discount in reverse futility, and no null move, when the opponent has an easy capture |
| `LdseMargin`, `LdseMode` | at low depth the hash move is extended when the static evaluation is well below alpha |
| `QsQuietHash` | in quiescence a quiet hash move with a lower bound is searched like a tactical move |
| `CorrFiftyStep` | the pawn and non-pawn correction tables are kept per band of the fifty-move counter |

Off, the tree is unchanged; each one changes it when switched on. They wait for their SPRTs.

**More speed, tree identical.** The fourth agent wrote branch-free versions of hot paths; each patch was measured on
its own with xperf, 6 rounds, PGO builds, 55.2 million identical nodes per engine:

| patch | build | cycles/node | mispredicts | kept |
|---|---|---:|---:|---|
| move generation: the moves of a piece written in four fixed groups of 16 squares, compacted with `vpcompressd` | avx512 | **−1.51%** | −3.4% | yes |
| network threat features: the two variable-count loops replaced by the same compaction | avx512 | **−1.11%** | −5.7% | yes |
| passed pawns of the network: a bitboard fill instead of a loop per pawn | avx512 / avx2 | **−0.81%** / **−0.54%** | −2.7% | yes |
| move ordering: rank of each move by mask and popcount, 16 compares at a time | avx512 | **−0.70%** | −1.8% | yes |
| make/unmake without branches on the capture | avx512 | −0.13% | −5.2% | no (noise) |
| TT: probe remembers the slot, store reuses it | avx512 | +0.42% | −1.6% | no |
| the move generation and threat patches in AVX2 form (`vpermd` and a 2 KB table) | avx2 | +0.29% / +2.29% | | no |

On AVX2 the table-driven compaction costs more than the branch it removes, so the AVX2 builds keep the loops and
gain only from the passed-pawn patch. The four kept patches are in the source; perft with a check at every node on
the six standard positions and the 300 positions of the Chess960 suite are exact.

## 21. The first idea adopted: a hash-move extension at low depth (5 October 2026)

After the leans of section 18 dissolved when tested together, the testing rule changed: **every search SPRT now runs
to at least 20,000 games** before it is judged. At this level a single search idea is worth one to three Elo, and a
test stopped at 2,000–3,000 games reads mostly noise.

**LDSE** (`LdseMargin`, `LdseMode`, section 20). Below the depth where the singular-extension test runs (here up to
depth 7), the hash move of a cut node is extended by one ply when its entry is a lower or exact bound and the static
evaluation is at least 80 below alpha. The entry says that move held a value; the static evaluation says the position
looks bad: the node stands on that one move, and near the horizon one more ply decides whether the value was real.
The idea appears in Reckless and Stormphrax; the form, the conditions and the values are ours.

| test | games | Elo |
|---|---:|---:|
| on against off, UHO 2024, 10+0.1, SPRT [0, 3] | 11,604 | **+4.5 ± 3.3** (LLR 2.04 of 2.94) |
| faster socket of the test machine (four memory channels, deeper search) | 6,644 | +6.8 ± 4.4 |
| slower socket (two memory channels) | 4,960 | +1.5 ± 5.1 |

The gain seems to grow with depth, which suits the longer time controls of the rating lists. It was stopped before
20,000 games and baked: **bench 498873** (141196 with `LdseMargin=0`).

**Tree size.** The bench total jumped, so the tree was measured position by position at fixed depth, on 80 positions:
the geometric mean of the node ratio (on / off) is ×1.06 at depths 8, 10 and 12 and ×1.02 at depth 14, with a median
near ×1.00. It does not grow with depth. A handful of positions explode, almost all rook endgames (×5 to ×16 at depth
12): there every cut node down a line extends again, and the depth falls by one ply every two instead of one per ply.
The bench jump is a single one of them. The fixed-time SPRT already paid that cost in the endgames its games reached,
and a test on the book of endgames confirmed it does no harm there: LDSE off against on, **−0.9 ± 4.1** (2,786 games,
10+0.1), that is LDSE on about +0.9.

**The audit's divergences as options.** Six of the seven divergences found in section 20 are now options, off by
default and tree-identical when off, each waiting for its own SPRT: `TTMoveRefresh` (a kept TT entry takes the new
move), `SeePinned` (in SEE a pinned piece does not recapture while its pinner stays), `HashQsDepth` (quiescence
entries at depth 0), `NullFiftyKeep` (the null move leaves the fifty-move counter alone), `EvalRefadeBridge` (one
fade formula), `TmFenPly` (the game ply counts the move number of a FEN). The time-management constants of the
increment branch are now parameters (`Tm*`, defaults identical), for a time-management SPSA played against an
external engine: in self-play the side that spends more time wins, and the tuner would reward it.

**Other tests of the day.** A quiet hash move searched in quiescence: **−1.05 ± 3.92** (2,984 games, endgame book),
closed as neutral. The pawn and non-pawn correction tables kept per band of the fifty-move counter: +2.1 ± 5.1 after
1,626 games on the endgame book, suspended to free the machine for the speed measurements below; the games are kept
and the test can resume. Neither moved the tree size at depths 10 to 16 beyond the ±10% resolution of that check, so
both stay at the short time control.

## 22. A guard for LDSE, the stack probe, and the expected reply (5 October 2026)

**A guard for LDSE.** The rook endgames that explode do so because the extension repeats down a line. `LdseMax` caps
the LDSE extensions on one line (0 = no cap, the baked default). With a cap of 1 the worst position at depth 12 falls
from ×15.7 to ×3.7 against LDSE off, but at depth 14 it is still ×11.6 against ×14.1: the cap helps without solving
it. Being tested against LDSE as baked, at 15+0.15 because both the gain and the explosions grow with depth: after
1,582 games **+6.2 ± 9.2**, the two sockets in agreement.

**The stack probe.** On Windows any function whose stack frame is larger than a 4 KB page calls `__chkstk`, which
touches each page before use. The disassembly of the release build showed it in two hot functions:
- `queue_next`, called once per move, with a 5,048-byte frame: the three functions that fill the move queue had been
  inlined into it together with their lists. They are now kept out of line; their own frames stay under 4 KB. The
  check evasions, the only list still ordered with 64-bit keys, moved to the 32-bit keys of the others: captures
  above all quiet moves with an offset of 2^20 instead of 2^28, same order.
- the network's `AccumulatorStack::evaluate`, called at every evaluation, with a 5,000-byte frame: the eight index
  lists of the two-perspective update (four of them 1.2 KB each) now live in the accumulator stack, one copy per
  thread, and are cleared at each use.

| build (release PGO, AVX-512, xperf, 6 rounds, 57.8 M identical nodes) | instructions/node | cycles/node |
|---|---:|---:|
| queue fill out of line | −2.76% | −0.81% |
| evaluation lists out of the stack | +0.64% | +0.09% |
| **both** | −2.63% | **−1.55%** |

The second change alone measures nothing, but together they gain almost twice as much as the first alone: one
reading is that the evaluation's frame only matters once the move queue no longer sits on the stack beside it. With
the speed work of section 20, about **−8.4% cycles per node since the 4 October pre-release**, tree identical.

**The expected reply.** When the opponent plays the reply the engine predicted on its principal variation, the hash
table is warm and the predicted move usually stands. The plan was to answer faster in that case. A measurement showed
the time management already does it, through its best-move stability rule: after a search to depth 19, the move
after the predicted reply took **0.23–0.50 s and stopped at depth 16**, while the same position searched from an
empty hash took 3.3 s and reached depth 17, sometimes choosing another move. The new levers therefore go the other
way, with a floor: after the expected reply the search does not stop before the previous depth minus `TmExpectFloor`
(within the maximum time). With the floor at 2 the same move took 0.55 s and reached depth 17; at 1, 0.80 s and depth
18. A reduction (`TmExpectScale`, with conditions on depth, move and evaluation) is there too. All are off by default,
with identical timings; the floor is next in the queue (SPRT at 12+0.12), and all four join the time-management SPSA.

Queue after the guard: the floor of the expected reply, no null move when the opponent has an easy capture,
`TTMoveRefresh`, `HashQsDepth`. The testing rule is now: run long, up to 20,000 games, but close earlier when the
interval excludes zero or the result is clearly flat.

## 23. The guard adopted, the surprise rule, and the refresh path (5 October 2026, evening)

**The guard adopted.** `LdseMax` = 1 (at most one LDSE extension per line) closed at **+6.25 ± 7.89** over 2,112
games at 15+0.15 against LDSE without a cap, both sockets positive (+7.4 and +4.6), and is now the default. New bench
**308883** (498873 without the cap).

**The expected reply, measured.** The floor (`TmExpectFloor` = 2) did not pay. Capped at the maximum time it lost
−5.5 ± 13.4 over 696 games at 12+0.12: the PGNs show the time spent early leaves about 20% less clock in the
middlegame. Capped at the optimum time it was flat at 16+0.16 (+0.6 ± 10.8, 1,090 games). It stays off.

**The surprise rule.** The author then turned the idea around: if the opponent does *not* play the reply the engine
expected, the position is less clear than it thought, so the move gets more time (`TmSurpriseScale`, ×1.2). The time
the stability rule saves on expected replies goes to the uncertain moves. The train of thought started from a remark
by **Mark Tang**: Stoofvlees answers very quickly between two moves, even at long time controls. In self-play the
expected reply is right about 74% of the time (48% in endgames), so about a quarter of the moves come after a
surprise.

| surprise rule | time control | result |
|---|---|---:|
| ×1.2 | 16+0.16 | +0.7 ± 7.1 (2,446 games) |
| ×1.6, only while the clock is above 40% of its start | 20+0.2 | −10.1 ± 7.5 (1,998) |
| **×1.2** | **40+0.4** | **+6.23 ± 6.75 (2,174), LOS ~96%** |

At 40+0.4 the two sockets agree (+6.2 and +6.3). The PGNs, written with principal variations, show where it comes
from: on the moves after a surprise the engine spends 12% more time and searches **0.35–0.4 ply deeper** than its
opponent does on its own surprises, while on expected replies it loses less than 0.1 ply. At 16 s the extra time does
not buy depth. ×1.2 is now the default. The lever depends strongly on the time control, so a short time-control SPSA
would push it back to 1.0: it is to be tuned only at the target time control, or kept fixed.

**The refresh path of the network.** A profile build counted why the accumulator is rebuilt in full instead of
updated. 55–58% of full rebuilds happen because the position before the king move was never evaluated; 21% because
fewer than 15 pieces are left. The hybrid update, which keeps the threat features and rebuilds only the king-relative
block, costs nearly as much as a full rebuild here: it rebuilds that block twice from the refresh cache, and with four
phase experts the cache entries are usually many moves old. Two tree-identical changes were measured with xperf, at
rest, on release PGO builds, six rounds each on 30 middlegame positions and 30 endgames:

| change | cycles/node, middlegame | cycles/node, endgames | |
|---|---:|---:|---|
| hybrid update below 15 pieces too (the 15 came from Stockfish's network) | **−0.55%** | **−0.54%** | adopted |
| threat index tables replaced by a 16-byte entry and a popcount | +0.81% | +0.82% | rejected |

Bringing the earlier position up to date incrementally and then using the hybrid update was also tried: about 2.5%
more time in the rebuilds, rejected. The index tables were already in the first-level cache. With section 22, about
**−8.9% cycles per node since the 4 October pre-release**, tree identical.

**Later the same evening.** All three adoptions together against the morning build, same binary, at 10+0.1:
**+1.3 ± 8.0** over 1,902 games. No regression, which was the point of the check: at this time control the
surprise rule does not show. No null move when the opponent has an easy capture stopped at +1.2 ± 5.6 over
4,308 games, to be resumed. Three more speed reviews of the network and of the search hot spots produced six
tree-identical patches; measured at rest, none gained (from +0.07% to +1.19% cycles), confirming that on this
machine only removing work pays, while prefetching and reordering loads do not. A 300-game match against
Stockfish 19 at the conditions of the earlier gauntlet (133+1, TopGM 8-move book) followed (section 24).

## 24. Against Stockfish 19, and the printed scale (6 October 2026)

**The match.** 300 games against Stockfish 19 at the conditions of the morning gauntlet (133+1, TopGM 8-move book,
1 thread): **+3 =291 −6, −3.5 ± 6.0** (morning build: +4 =189 −7, −5.2 ± 11.3 over 200). On the 100 openings both
matches played, 98/200 against 98.5/200. Three morning wins became draws; the moves that differ come from LDSE (at
fixed nodes, the evening build with LDSE off reproduces the morning search exactly). Ten replays of each of the two
openings with each build settled it: as White both builds win the Modern with f4 every time (5/5 and 4/5) and the
Italian with Bxf7+ rarely (0/5 and 1/5). The morning wins were chance, not lost strength. Half of the decisive games
against Stockfish are decided as the book ends (both engines see ±1.2 to ±1.7 at move 9); in the rest, Stockfish
converts an advantaged side better than we do (in the same Italian, as White it won 6 of 10 replays, we won 1).

**Do we see Stockfish's advantage late?** In the lost games Stockfish's evaluation crossed ±1.00 about seven moves
before ours. Most of that is the printed scale: over 24,761 consecutive positions Stockfish prints 1.26 times our
number. On 15 of those positions, at the same depth and rescaled, the two evaluations agree (−0.92 against −0.95); the
rest is depth. Our self-play games put a 50% win chance at +0.89 printed, at every amount of material, at 10+0.1 and
at 40+0.4, so the printed centipawns are now divided by 400 instead of 449: **+1.00 means a 50% chance to win**.
Display only, bench unchanged. Rescaled, Stockfish's lead in the lost games falls from 7.4 to 1.3 moves. None of the
losses after an even book exit was caused by a pinned piece.

## 25. Ideas that need depth, pins, and the correction history (6 October 2026)

**Two adoptions, and what they showed.** The quiescence search now stores and reads depth 0 in the hash instead of −1
(`HashQsDepth`): **+3.38 ± 2.58** over 20,162 games at 10+0.1, adopted (bench 222624). In the static exchange
evaluation, pieces pinned to their king no longer recapture while the pinner is on the board (`SeePinned`):
**+4.5 ± 6.8** over 2,934 games, adopted (bench 507070); at fixed depth its tree is unchanged in most positions
(median ×1.00 at depths 10–16). Both gained on one socket only: +1.0 and −2.1 on socket 0, +6.6 and +13.4 on socket
1. Socket 0 ran 40 games on 20 cores with hyperthreading and searched about 0.4 ply less deep than socket 1, so its
10+0.1 was effectively shorter. **Ideas that act deep in the tree show up only when the search goes a little
deeper**: from here on, search SPRTs run at 15+0.15 (0.5–0.9 ply deeper on both sockets), the others at 10–12 s.
Saturating socket 1 (two memory channels) costs depth too, so both sockets now run 34 games at once.

**Closed, and left off.** Refreshing the hash move when a deeper entry is kept: −0.65 ± 2.77 over 17,574 games. No null
move when the opponent has an easy capture: −0.34 ± 4.04 over 8,164. Ideas retried on the restructured search at
15+0.15: no "improving" discount in reverse futility when the opponent has an easy capture −3.5 ± 8.4 (1,704 games,
both sockets negative); the transition extension −18.2 ± 16.4 (402), likely inflating the tree together with the
other extensions; the passed-pawn push in quiescence −0.77 ± 7.48 (2,264), flat, to be retried with more games.

**Two consistency fixes.** The null move no longer advances the fifty-move counter, and an evaluation read back from
the cache or the hash is re-faded with exactly the formula used when it was computed. Both act only with a high
fifty-move counter, so they were measured where that happens: on the endgame book, **+1.6 ± 2.6** over 9,774 games
at 5+0.05 (−2.2 ± 7.5 on UHO at 6+0.06, no harm in the middlegame). Adopted (bench 370886); at fixed depth the
endgame tree gets smaller as depth grows (×0.91 at depth 16).

**The correction history, studied.** Corrections separated by network expert (`CorrPhase`: the pawn and minor-piece
correction keys salted with the expert, so each expert learns its own error) first came out at **−5.1 ± 8.1** (1,976
games). An idea with that rationale should not lose, so before closing it an analysis agent read the whole correction
code. It found the cause: the move generator prefetched the correction rows with the unsalted keys, so with the option
on, the rows actually read were never prefetched and two useless loads were issued. The same defect had penalised
the fifty-move-band corrections (+1.25 ± 2.87 over 5,294, closed neutral before the fix). Fixed, with an identical
tree when the options are off, `CorrPhase` measured **+3.2 ± 3.8** over 8,634 games at 12+0.12, both sockets
positive, and is now the default (bench **309067**). Rule kept from this: an idea meant to gain Elo that comes out
flat is analysed before it is closed.

The agent's other proposals went into the source as options, all off: a continuation correction six plies back,
conditions on learning, a correction faded with the fifty-move counter like the evaluation (our idea), an additive
per-expert table, and the update constants that came with the restructured search, now parameters for an SPSA. A
diagnostic build measures how much of the evaluation error the corrections remove (200 positions, 1M nodes each, exact
nodes): **58.8% with `CorrPhase` against 57.3% without**, the gain coming from endgames with nine pieces or fewer
(residual rms 110.6 against 131.6). After the correction the mean residual is similar for every expert, so an
additional per-expert table has little left to learn. Positions with a high fifty-move counter are rare even in
endgame games (fewer than 2% of exact nodes), which fits the result of the correction faded with the counter:
**−2.7 ± 4.3** on the endgame book (2,572 games), closed. The correction slot shrank from 10 to 8 bytes (the field of
a closed idea removed): neutral in speed (cycles per node +0.00% middlegame, +0.26% endgames), kept as a clean-up.

## 26. The opponent's plan, a combined SPSA, and a progress check (6 October 2026, evening)

**Two more continuation corrections.** The continuation correction indexes the last move by our moves two and four
plies back. Adding our move six plies back (`CorrCont6W`) gave **−0.8 ± 5.6** over 3,894 games at 15+0.15, with the
two sockets in disagreement (+4.3 and −5.9): off, to be looked at again. The author then proposed the dual idea: if,
while searching the opponent's reply, a move turns out better or worse than expected, the correction should learn
that too. It was built as the pair of the opponent's own last two moves (three plies back and one ply back), in the
same table (`CorrContOppW`). At the weight of the existing terms it gave **−5.2 ± 12.7** over 742 games at 12+0.12.

**Analysed before closing it.** A diagnostic build let those cells learn at weight 0 and compared them with the error
left after the existing corrections (600 positions, 1M nodes each). They carry real information: correlation +0.24
with the remaining error in exact nodes, as much as the two-ply cell (+0.25). With the term on, the tree at a fixed
node count is unchanged (depth 21.25 against 21.26) and the speed cost is about 1% under full load. A regression
suggested twice the weight; at that weight it gave **−4.8 ± 8.7** over 1,806 games at 8+0.08. The same regression,
read correctly, says that every correction weight should be larger, the two-ply one about twice its value, while the
SPSA, which optimises games won, chose half of that: reducing the evaluation error is not the same as winning, because
the correction also moves pruning margins and at fail-high or fail-low nodes the error is only a bound. So the weight
is left to an SPSA: `CorrContOppW` starts there at the first weight, with zero (off) within its range.

**Conditions on learning.** No learning in nodes searched with a move excluded (`CorrLearnMode` 2): **−1.6 ± 7.8**
over 2,114 games at 12+0.12, off. Variant 1 (only excluded nodes without moves) never fires on the bench positions.

**A combined SPSA, CORR1.** 29 parameters at 20+0.2: the 17 constants of the correction history (update steps,
bonuses and their caps, weights, the divisors of the correction in the singular, reverse-futility and reduction
margins), the opponent's-plan weight and step, and ten reduction, reverse-futility and singular parameters that
depend on the corrected evaluation. Parameters that depend strongly on the time control stay out (the depth ramps and
the logarithmic reduction multiplier); the final values will also be checked at 40+0.4 before adoption.

**Progress since the 4 October prerelease.** The 4 October prerelease was rebuilt for AVX-512 from the tag `v8.0`
(the published AVX2 build gives the same `bench`, 141196) and played against today's development build (bench
309067), each with its own defaults: **+17.2 ± 6.8** over 2,798 games at 10+0.1 (UHO, 34 games per socket; socket 0
+20.5, socket 1 +13.7). It is the sum of everything since 4 October: the ideas adopted in sections 21–25 and the
speed work of sections 17–23.

**Against Stockfish 19 again, with the current build.** The release built from this source (bench 309067) played the
same 300 games as in section 24 (133+1, TopGM 8-move book, openings 1–150 with both colours, 1 thread, Hash 512,
Syzygy 3-4-5), then the first 20 games of openings 151–155 and 226–230: **+5 =310 −5 over 320, 50.0%** (the 300 of
section 24: +5 =290 −5, 0.0 ± 5.6, against +3 =291 −6, −3.5 ± 6.0 for the 5 October build; the 20 new games all
drawn). Every loss and every changed result was checked move by move with Stockfish 19 at 5 s per position. Three
of the five losses were decided by the book (Stockfish gives Black −1.15, −1.50 and −1.72 as the book ends, and finds
no move of ours worse than its own by 0.20 before the game is lost); one had an unfavourable book exit (−0.82) plus
two small inaccuracies of about 0.3. One was lost by a single move in a rook and
knight endgame: with Stockfish at −0.24, our king walked to the centre (66...Ke5 instead of Kg6) into the reach of
both rooks and the knight (−2.48). We chose it at depth 22; replayed from the same game history with the same time,
our engine played Kg6 twice, and in one of the replays it preferred Ke5 at depths 8–12: the refutation is found late
and not every time (analysed in section 27). The two
5 October wins that became draws contain no move of ours that Stockfish rates 0.20 worse than its own.

## 27. One lost game, examined, and the clock (6 October 2026, night)

**The game.** Of the five losses against Stockfish 19 in section 26, one came from a single move with the position
still holdable: a rook and knight endgame where our king walked to the centre (66...Ke5 instead of Kg6) and was
hunted down for ten moves until a skewer won a rook. Three analyses, each run before drawing a conclusion:
- **The move itself.** Replayed with the same game history and the same time, our engine plays Kg6 or Kg5 22 times
  out of 22. In the game it had about 4.5 s on the clock and the hard time limit stopped the iteration; searched
  alone, Ke5 is refuted clearly from depth 20. A rare instability under time pressure, not a pattern: no other game
  of either match shows it. A cap on root reductions changes nothing measurable (a reduced root move that looks good
  is always searched again at full depth); it stays in the code as an option, off.
- **Checks.** Do we reduce or prune the checks of a king hunt too much? No. From depth 20 every move of the
  refutation is the first one tried, is never pruned or reduced, and gets 1 to 2.6 plies of singular extension.
  Meaningless tweaks of unrelated parameters move the depth at which a single position is solved by up to seven
  plies, so every lever was judged against such controls on a set of 19 positions: none stood out. Extending checks
  costs 1.45 plies in endgames.
- **Endgames.** Do we recognise won endgames later than Stockfish? Not on 124 decided endgames: our static
  evaluation is 0.90 of Stockfish's, which is only the difference in printed scale (Stockfish prints about 1.12 times
  our number since our centipawns are divided by 400), and in nodes we reach a decisive score no later.

**The clock.** Over 820 games against Stockfish 19 at 133+1 (clocks rebuilt from the game records and checked to the
millisecond on 17,850 moves where the GUI logged them), our time manager is the same function of clock and move
number as Stockfish's. We spend more in the longest thinks of moves 11–40 and so reach moves 40–60 with 3–4 s less in
over half of the games, and errors concentrate when the clock is under three increments (17–30 per 1,000 moves,
against under 1 above 60 s). Lowering the cap on the longest thinks lost clearly: **−27.1 ± 17.0** over 308 games at
40+0.4 (both sides with the game's move number, as in a PGN book). The long thinks are needed; the opposite
direction is under test.

**The TT cutoff damping adopted.** The damping of section 18 (a lower-bound cutoff value from the hash is pulled
towards beta; an idea from Coda), retried on the current code: +2.2 ± 5.4 over 4,138 games at 15+0.15 and
+2.0 ± 5.0 over 4,818 at 20+0.2, both sockets positive. With the first test the three give **+2.2 ± 3.2 over
11,774 games** (about a 91% chance of a positive effect, under 3% of costing more than one Elo): adopted before
20,000 games on that evidence. Bench **430151**.

**The search side made faster.** The search costs about 3,200 cycles per node against about 1,750 for Stockfish 19,
spread over move ordering, the TT, the SEE and make/unmake with no dominant function. An analysis agent counted how
often each step runs per node and wrote nine patches that only remove work, each with the same tree: pawns without
moves are no longer visited, the SEE of a quiet move is skipped when the enemy attack map already decides it (28% of
all SEE calls), empty-board slider attacks come from a small table, the common case of two per-move functions is
inlined, the TT store reads its bucket once, the piece type comes without a division, and the expert key of the
corrections from a table. Every patch kept the bench and passed perft and Chess960 perft; three of them carry a
build switch that compares the shortcut with the original code at every node. Measured with hardware counters on a
quiet machine, the nine together remove 5.4% of the instructions but only 1.0–1.5% of the cycles in the middlegame
(1.5–1.8% in endgames): the engine waits on memory more than it executes. One patch scored sixteen quiet moves at
a time with AVX-512 gathers; it removed 3.9% of the instructions, but on this Xeon a gather costs as much as sixteen
scalar loads, and without it the other eight give **−1.75% cycles per node** in the middlegame. Those eight were
adopted. Bench unchanged, 430151.

**Also closed today.** Learning corrections in exact PV nodes in either
direction: −2.8 ± 6.6 over 3,258 at 8+0.08, off. Three adopted options (quiescence hash depth, the two consistency
fixes) became fixed code; in release builds the remaining tuning copies read outside the search are compile-time
constants (bench unchanged by these, 309067 before the damping).

## 28. One board for the search and the network (7 October 2026)

**The starting point.** The network code came from Stockfish together with its own board: every thread kept a
second position, a Stockfish `Position`, next to the search's board. Each move made by the search was translated
into that second board (squares mirrored vertically, piece codes remapped), and from that copy the network derived
what a move changes for its inputs: the piece that moves, the pawn features and the threat features. Since July the
copy was lazy: a move was applied to it only when an evaluation actually needed it, so the many nodes that end
before evaluating (hash cutoffs, nodes in check, pruned moves) never touched it. The aim, chosen by the author, was
to remove the second board by changing the network side, so that the network reads the search's board directly and
the inference code becomes our own.

**Measuring.** Every version below keeps the tree identical (bench 430151 on AVX2, AVX-512 and release builds), and
correctness was checked the same way each time: perft with the full accumulator compared against a fresh refresh at
every node (126 standard positions at depth 4, all 960 Chess960 start positions at depth 3, 120 of them at depth 4,
over 100 million evaluations each time), incremental against refresh in two-thread searches, UCI play identical to
the reference, and static evaluations identical on 2,855 positions. Speed was measured with hardware counters on a
quiet machine (PGO release builds, six alternating rounds on 30 middlegame and 30 endgame positions, AVX-512); the
noise of this measurement is about ±0.3% in cycles.

**First version: the copy removed, the translation kept.** The network read the search's board through a thin view,
and before an evaluation the pending moves were replayed: the search's mailbox was walked back to the last computed
accumulator and then forward, move by move, computing what each move changes. Correct everywhere, and slower:
+4.2% instructions and +1.0% cycles per node in the middlegame, +4.6% and +1.5% in endgames. The translation had
only moved: the two boards number their squares in mirrored order, so every bitboard read now
paid a byte swap, and every pending move was played twice.

**Second version: one numbering, the changes written by the make.** The tables that turn a piece on a square into a
feature index were rebuilt in the search's own numbering (the mirror is applied once, when the tables are built, so
every feature index stays the same and the network file is unchanged), and the search's make writes what a move
changes onto a per-thread stack, as Stockfish's `do_move` does. The old `Position` code and three debugging tools
that depended on it were removed: 41 files, +1,239 / −3,289 lines. A build switch checks at every evaluation that
the stack describes the search's board, and a deliberately broken make was caught at the first bench. Two variants:
threats computed in every make (a), or computed only before an evaluation from a 64-byte copy of the board saved by
the make (b).

| per node, against the old code | middlegame instr. | middlegame cycles | endgame instr. | endgame cycles |
|---|---:|---:|---:|---:|
| first version | +4.19% | +1.04% | +4.64% | +1.47% |
| second version (a) | +2.68% | +0.89% | +3.40% | +1.18% |
| second version (b) | +2.71% | +1.22% | +3.19% | +1.31% |
| (c): only the moved piece in the make | +0.60% | +0.59% | +1.13% | +0.52% |
| (c) with a branch-free refresh | +1.21% | **−0.41%** | +1.66% | **−0.51%** |

**Why "like Stockfish" was slower here.** Stockfish's make computes the threat changes on its only board, where that
work replaces nothing. Here the search's make already existed and did its own work, so variant (a) added the threat
computation to every move, including the moves whose nodes never evaluate, which the lazy copy had never paid for.
Variant (b) evaluated lazily but paid for saving the board in every make and for reading pieces from bit planes.

**Variant (c).** The make records only the moved piece (from, to, capture, promotion, castling, material band).
Before an evaluation, the board before the first pending move is rebuilt from the search's current board by undoing
the pending moves on local copies (XOR on the bitboards, a 256-byte copy of the mailbox), and the moves are then
replayed forward computing pawns and threats with the same fast mailbox read as (a). In most nodes the parent was
already evaluated, so a single move is pending. This brought the extra instructions from +2.7% down to +0.6–1.1%,
but the cycles stayed +0.5–0.6% above the old code.

**The branch mispredictions.** In every second version, mispredicted branches per node rose by 5–6% (about two per
node), present in both (a) and (c), so not tied to where threats are computed. The cause was the comparison between
an entry of the accumulator refresh cache and the current position, rewritten in the second version as twelve
iterations, one per piece type, each with two loops whose trip count depends on the position and is almost always
zero: 24 hard-to-predict branches on every refresh, and refreshes are frequent (every king move across a bucket and
every change of material band). Replaced by a branch-free XOR and OR of the twelve bitboards that gives the changed
squares at once, followed by two loops (squares to remove, squares to add) with the piece read from three bit planes,
the mispredictions fell from +6.2% to +1.9% and the cycles went below the old code: **−0.41%** in the middlegame and
**−0.51%** in endgames, with more instructions (+1.2–1.7%) executed at a higher rate (IPC 1.33 → 1.36). The order of
the indices changes, but an accumulator is a sum of integers, so the result is identical in every bit.

**A branch-free step that lost.** The undo and redo of the pending moves still branched on "is there a capture, a
promotion, a castling rook". These were made unconditional: a table maps the "no square" value 64 to an empty
bitboard, so the XOR always runs, and the local mailbox got a 65th cell that absorbs writes to "no square". All
checks passed, instructions and mispredictions went down (+1.04% and +1.45% against the old code), and the cycles
went up: +0.80% in the middlegame and 0.00% in endgames, about one point worse than the version above. At that point
there is almost always one pending move of the usual kind, so those branches were well predicted and cost nothing;
the replacement added table loads on the critical path and stores that the vector code reads back at once in 64-byte
blocks. A branch costs only when it is mispredicted: the refresh comparison had 24 unpredictable branches, these had
none. Reverted.

**Result.** Variant (c) with the branch-free refresh comparison is the version kept: the network reads the search's
board with no second copy and no translation, 2,000 fewer lines, and **0.4–0.7% fewer cycles per node** than the old
code with the identical tree (−0.40% and −0.69% measured again on the integrated source). It replaced the old code in
the development source on 7 October, with the stack check kept as a build switch, and it is in the 7 October
pre-release.

## 29. Fine-tuning the network, a measurement artefact, and LDSE at a long time control (7–8 October 2026)

**Fine-tuning.** Three attempts were made to add strength to Consilium without a new training run, on rented GPUs.
(1) Each expert trained alone on the positions of its own material band, the other experts frozen, 8 epochs at a
peak learning rate well below the end of the original schedule: +0.7 ± 6.1 Elo over 3,302 games at 15+0.15.
(2) The whole network, 15 epochs on new positions of the same family (relabelled Leela data, Fischer random data
included): the mean of the last two epochs gave −0.1 ± 3.2 over 13,807 games at 6+0.06. Its evaluations differ from
Consilium's by 1% in scale, with a correlation of 0.9994 on 3,007 positions from real games: more epochs on data of
the same family move a converged network very little. (3) A small extra input block on the two experts with many
pieces, outposts (a knight or bishop on the fourth to sixth rank, supported by a pawn and out of reach of enemy
pawns, 512 inputs), trained alone with the rest frozen. The training loss fell by 0.25% in two epochs and then stayed
flat; in play the block cost more than it gave, −6.4 ± 3.9 over 9,392 games at 6+0.06. None of the three was kept.
The engine side of the extra block (an optional input segment, switched on when the network file contains it) stays
outside the published source.

**A measurement artefact.** In the second test the two sockets of the test machine disagreed by 20 Elo with the
same pair of networks (−0.1 ± 3.2 and −20.8 ± 3.6), and the gap was constant from the first games to the last. The
cause was in the test, and partly in the engine. The match program sends every option again after each `ucinewgame`,
and the engine reloaded the network file each time it received `EvalFile`, also when the file was the one already
loaded: about 0.6 s and 245 MB of large pages allocated and freed before every game, on one side only, since the
other side used the default network. On the socket with less memory the reloading side searched 0.22 plies less on
average than its opponent (0.02 on the other socket). The same happened to `Hash`: the transposition table was freed
and allocated again before every game. Both handlers now do nothing when the value is the one in use (the hash table
is still cleared by `ucinewgame`). The engine also no longer gives up on the shared copy of the network after two
minutes of waiting for another process that is creating it; it waits and checks once per second whether the copy is
ready. With the fix every engine process in a 140-process match keeps exactly one load and the same memory footprint,
and the two sides search to the same depth on both sockets. Earlier network tests at 10+0.1 show a small version of
the same asymmetry (0.03 plies); tests of search options, which use the same network on both sides, are unaffected.

**LDSE at a long time control.** The hash-move extension at low depth (section 21) was adopted at 10+0.1. Switching
it off at 40+0.4 gave −1.15 ± 3.45 over 9,348 games: it stays on.

## Appendix: every search idea tested since the restructured search

One line per idea, in the order tested; details in the section given. Elo is the candidate against the defaults on
the same binary, with its 95% interval; "lean" means stopped early while positive.

| idea | section | TC, book | games | Elo | outcome |
|---|---|---|---:|---:|---|
| Disagree (prune less where the network's outputs disagree) | 18 | 10+0.1 UHO | 690 | −12.6 ± 15.2 | removed |
| PhaseEdge margin / improving | 18 | 10+0.1 UHO | 1,262 / 296 | −2.5 / −14.1 | removed |
| PhaseEdge, no futility across experts | 18 | 10+0.1 UHO | 24,292 | +1.3 ± 2.3 | neutral, off |
| LateBranch | 18 | 10+0.1 UHO | 892 | −5.1 ± 12.9 | removed |
| Near-miss TT cutoffs (corrected port) | 18 | 20+0.2 UHO | 1,142 | −6.7 ± 10.6 | removed |
| TT cutoff damping | 18 | 15+0.15 UHO | 2,768 | +2.6 ± 6.8 | lean, off |
| Defensive replies / endgame null-move verification | 18 | 10+0.1 endgames | 1,990 / 728 | −2.3 / −6.7 | removed |
| The five leans together | 18 | 10+0.1 UHO | 3,120 | −0.1 ± 6.7 | nothing baked |
| Depth ramp RW1 → LTC1 | 18 | 20+0.2 UHO | 1,486 | −0.9 ± 9.2 | off |
| LTC1 SPSA vector (mean of the last 50) | 19 | 30+0.3 UHO | 606 | −4.6 ± 14.1 | RW1 values kept |
| **LDSE**, hash-move extension at low depth | 21 | 10+0.1 UHO | 11,604 | **+4.5 ± 3.3** | **adopted** |
| Quiet hash move in quiescence | 21 | 10+0.1 endgames | 2,984 | −1.05 ± 3.92 | closed |
| **LDSE guard** (one extension per line) | 23 | 15+0.15 UHO | 2,112 | **+6.25 ± 7.89** | **adopted** |
| Expected-reply floor | 23 | 12–16 s UHO | 696 / 1,090 | −5.5 / +0.6 | off |
| **Surprise rule ×1.2** | 23 | 40+0.4 UHO | 2,174 | **+6.23 ± 6.75** | **adopted** |
| The three adoptions together, against the morning build | 23 | 10+0.1 UHO | 1,902 | +1.3 ± 8.0 | no regression |
| Hash-move refresh on a kept entry | 25 | 10+0.1 UHO | 17,574 | −0.65 ± 2.77 | off |
| **Quiescence hash depth 0** | 25 | 10+0.1 UHO | 20,162 | **+3.38 ± 2.58** | **adopted** |
| No null move against an easy capture | 25 | 10+0.1 UHO | 8,164 | −0.34 ± 4.04 | off |
| **Pins in SEE** | 25 | 10+0.1 UHO | 2,934 | **+4.5 ± 6.8** | **adopted** |
| Corrections per fifty-move band (before the prefetch fix) | 25 | 10+0.1 endgames | 5,294 | +1.25 ± 2.87 | off |
| Reverse futility against an easy capture | 25 | 15+0.15 UHO | 1,704 | −3.5 ± 8.4 | off |
| Transition extension | 25 | 15+0.15 UHO | 402 | −18.2 ± 16.4 | off |
| Passed-pawn push in quiescence | 25 | 15+0.15 UHO | 2,264 | −0.77 ± 7.48 | off, to retry |
| `CorrPhase` (before the prefetch fix) | 25 | 12+0.12 UHO | 1,976 | −5.1 ± 8.1 | analysed |
| **Two consistency fixes** (null move and fifty-move counter, one fade formula) | 25 | 5+0.05 endgames | 9,774 | **+1.6 ± 2.6** | **adopted** |
| **`CorrPhase`** (after the fix) | 25 | 12+0.12 UHO | 8,634 | **+3.2 ± 3.8** | **adopted** |
| Correction faded with the fifty-move counter | 25 | 10+0.1 endgames | 2,572 | −2.7 ± 4.3 | off |
| Continuation correction six plies back | 26 | 15+0.15 UHO | 3,894 | −0.8 ± 5.6 | off, sockets disagree |
| The opponent's plan in the continuation correction (author's idea) | 26 | 12+0.12 UHO | 742 | −5.2 ± 12.7 | analysed, to the SPSA |
| The same at the regression weight | 26 | 8+0.08 UHO | 1,806 | −4.8 ± 8.7 | off |
| No correction learning in excluded nodes | 26 | 12+0.12 UHO | 2,114 | −1.6 ± 7.8 | off |
| **Today's build against the 4 October prerelease** | 26 | 10+0.1 UHO | 2,798 | **+17.2 ± 6.8** | progress check |
| Hash cutoff damping, retried on the current build | 27 | 15+0.15 UHO | 4,138 | +2.2 ± 5.4 | retried at 20+0.2 |
| **Hash cutoff damping** at 20+0.2 (three tests together: +2.2 ± 3.2 over 11,774) | 27 | 20+0.2 UHO | 4,818 | **+2.0 ± 5.0** | **adopted** |
| Corrections learned in exact PV nodes in either direction | 27 | 8+0.08 UHO | 3,258 | −2.8 ± 6.6 | off |
| Lower cap on the longest thinks (time manager) | 27 | 40+0.4 UHO | 308 | −27.1 ± 17.0 | off |
| LDSE switched off, long time control | 29 | 40+0.4 UHO | 9,348 | −1.15 ± 3.45 | LDSE kept |
