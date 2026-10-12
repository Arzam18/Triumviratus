<div align="center">

<img src="logo.png" alt="Triumviratus" width="200">

# Triumviratus

**A strong UCI chess engine in C++** — NNUE evaluation · SPSA-tuned alpha-beta search · Syzygy tablebases

[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg)](COPYING)
[![C++](https://img.shields.io/badge/language-C%2B%2B-00599C.svg)](source/)
![UCI](https://img.shields.io/badge/protocol-UCI-brightgreen.svg)
![NNUE](https://img.shields.io/badge/evaluation-NNUE-orange.svg)

**by Francesco Torsello**

<sub>in collaboration with Maurizio Platino</sub>

</div>

---

<div align="center">

[Rating](#rating) · [8.0 (in development)](#triumviratus-80--in-development) · [Earlier releases](#earlier-releases) · [Dev log 8.0](DEVELOPMENT_8.0.md) · [Dev log 7.0](archive/DEVELOPMENT_7.0.md) · [Dev log 6.0](archive/DEVELOPMENT_6.0.md) · [Networks](NETWORKS.md) · [What is new](NOVELTIES.md) · [Future directions](FUTURE_DIRECTIONS.md) · [Tests](tests/) · [History](HISTORY.md) · [License](#license) · [Credits](#credits)

</div>

---

## Rating

**CCRL Blitz** (2 min + 1 s):

| Version | Rating | Rank | List |
|---|---|---|---|
| **Triumviratus 7.0 64-bit (1 CPU)** | **3757** ±19 | **8** | 2026-09-26 |
| Triumviratus 6.0 64-bit (1 CPU) | 3749 ±13 | 12–14 | 2026-08-16 |
| Triumviratus 4.2 64-bit (1 CPU) | 3670 ±13 | 51–52 | 2026-08-08 |

**CCRL 40/15** (40 moves in 15 minutes + increment):

| Version | Rating | Rank | Games | List |
|---|---|---|---:|---|
| **Triumviratus 6.0 64-bit (4 CPU)** | **3632** ±17 | **13–14** | 678 | 2026-09-23 |
| Triumviratus 5.1 64-bit (1 CPU) | 3605 | — | — | 2026-07-23 |
| Triumviratus 5.0 64-bit (4 CPU) | 3603 | — | — | 2026-07-16 |
| Triumviratus 5.0 64-bit (1 CPU) | 3570 | — | — | 2026-07-16 |

<sub>Ranks are tie bands shared by the engines inside the interval: read the rating, not the rank. On
40/15, 6.0 sits 18 Elo behind the first entry (Stockfish 19 4 CPU, 3650 ±19). On Blitz, 7.0 (750 games
so far) is eighth among the best version of every engine, single- and multi-CPU mixed, ahead of
several 8-CPU entries; it has not appeared on 40/15 yet. The two lists are not comparable with each
other.</sub>

---

## Triumviratus 8.0 — in development

### Consilium

**Consilium**, the 8.0 network, is to our knowledge the first mixture-of-experts network released in a top engine
and the first shown to gain strength. The network's largest block, the king-relative input layer, is split into
**four experts, one per phase of the game** (chosen by the number of pieces), and only one is active at a time, so
the network has four times the weights in that layer at almost the cost of one. The idea is the author's own, from
the mixture-of-experts models of language AI. Consilium with its parameters re-tuned is **+27.3 ± 8.3** Elo over
7.0; a study of its first layer neuron by neuron found no dead or duplicate neurons and experts that specialise.
[How it was built and trained](NETWORKS.md#consilium--the-triumviratus-80-network) ·
[why it is new](NOVELTIES.md#1-consilium-experts-by-material-on-the-first-layer)

### What is new in Triumviratus

[`NOVELTIES.md`](NOVELTIES.md) compares every technique of the engine with the published work of other engines: of
52 entries, 10 are new, 26 are known ideas in a different form, 16 were done elsewhere first. The new ones in use:

- **Consilium**, experts by material on the first layer (above);
- **`PassedPawns`**, an input block of our own for passed pawns, in every network since 6.0
  ([§2](NOVELTIES.md#2-the-passedpawns-input-block));
- **corrections per expert** (`CorrPhase`): the correction history follows the experts of the network
  ([§3](NOVELTIES.md#3-correction-history-per-expert-corrphase));
- **move ordering with chess knowledge**: knight outposts and the capture of a bishop of the pair
  ([§4](NOVELTIES.md#4-move-ordering-with-chess-knowledge)).

Among the known ideas in a new form, the author's own: the **surprise rule** of time management (more time when the
opponent does not play the expected reply) and the **causal reduction** of quiet moves. Ideas measured and dropped
are recorded too, with their numbers.

### Measurements

| step | result |
|---|---:|
| speed: faster code, identical search tree, new transposition table | +14.7 ± 5.4 against 7.0 |
| **Consilium**, with its parameters re-tuned | **+27.3 ± 8.3** against 7.0 |
| **restructured search**, re-tuned on our network | **+85.8 ± 12.8** against the previous 8.0 |
| pre-releases of 10–12 October: move ordering with chess knowledge, the causal reduction, rules from Stockfish 19, time management in defence | +11.9 ± 7.2 against the 9 October pre-release, then each change by SPRT |

8.0 is also the first version to support **Chess960**.

**Against other engines** (1 thread unless stated; details and games in [`tests/`](tests/) and
[Triumviratus-Testing](https://github.com/Tors3/Triumviratus-Testing)):

| tester | test | build | result |
|---|---|---|---|
| Maurizio Platino | **Stockfish 19**, 300 games, 1 min + 1 s, 3 threads | 8 October | **−6.9 ± 9.6** |
| Maurizio Platino | Stormphrax 8.0.0 / Reckless 0.10.0-dev, 300 games each | 4 October | **+94 ± 16** / **+25.5 ± 14.6** |
| Maurizio Platino | ENET 2026 suite | 4 October | **89 / 110**, best Triumviratus so far |
| Mark Tang | IQ4 tactical suite, 1 s per position | 5 October | **145 / 183**, highest among the engines tested |
| Andres (EveAnn) | gauntlet against 15 engines, 1,500 games, 2 min + 0.4 s | 11 October | **62.8%, +91.2 ± 9.4**; **Stockfish 19** +8 =84 −8 |
| the project | Stockfish 18, 600 games, 20+0.2 / Reckless 0.10.0-dev, 526 games, 40+0.4 | 7–8 October | **+24.9 ± 13.7** / **+68 ± 14** |

The current pre-release (12 October, one executable for every x86-64 CPU) is the tag
[`v8.0`](https://github.com/Tors3/Triumviratus/releases/tag/v8.0); `source/` holds the development code. Every
measurement, the ideas adopted and those dropped: **[`DEVELOPMENT_8.0.md`](DEVELOPMENT_8.0.md)** ·
**[`NOVELTIES.md`](NOVELTIES.md)** · **[`NETWORKS.md`](NETWORKS.md)** · **[`FUTURE_DIRECTIONS.md`](FUTURE_DIRECTIONS.md)**.

---

## Earlier releases

- **7.0** (tag `v7.0`, current CCRL entry): the network **`legio-septima`**, the first trained from scratch with all
  its blocks together, including our own **`PassedPawns`**; **+21.3 ± 6.7** over 6.0 at 25+0.25 (3,000 games), fourth
  most aggressive of 16 engines on the [EAS ratinglist](https://www.sp-cc.de/eas-ratinglist.htm).
  [`archive/DEVELOPMENT_7.0.md`](archive/DEVELOPMENT_7.0.md)
- **6.0**: the `TRANN1` architecture, the `nn-rubicon-alea-v3` network and TMv2 time management; **+53.0 ± 12.3**
  over 5.1 at 40+0.4 (760 games). [`archive/DEVELOPMENT_6.0.md`](archive/DEVELOPMENT_6.0.md)

---

## License

[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg)](COPYING)

> [!IMPORTANT]
> **GPLv3** — see [`COPYING`](COPYING). The **NNUE inference code** started from **Stockfish** (the SFNNv16 evaluation machinery in `nnue/`, GPLv3). For our own mixture-of-experts network, Consilium (architecture TRANN3), most of the part that runs at every node has since been rewritten: the accumulators, the feature blocks and the link with the search. The forward pass (layers, SIMD) and the net loading are still Stockfish's; about 60–70% of the inference lines remain in common with Stockfish (see [`source/nnue/README.md`](source/nnue/README.md) and [Credits](#credits)). Triumviratus' search was restructured in October 2026. Nearly all of its structures were already in the engine, but disordered and clogged by parameters and tests accumulated one at a time since version 5.0. After studying the searches of Stockfish and Reckless, it was reorganised following the structure of Stockfish 19's search (GPLv3), keeping our own ideas, with a complete SPSA re-tune on the new MoE network. It is Triumviratus' own code, with techniques of our own such as passed-pawn pushes in endgames, and our own data structures, move generation, evaluation and network. Of the two extra NNUE input blocks: **`PassedPawns` is an original feature of this project**, whereas **`PawnPair` implements a pawn-pair input feature that is shared across several open-source engines** (Stormphrax, Viridithas, Pawnocchio — see [Credits](#credits)); its C++ implementation and its trained weights are the project's own, but the feature *design* is not. The shipped network was trained by the project (see [`NETWORKS.md`](NETWORKS.md)). Because the engine incorporates Stockfish's GPL code, **the whole project is distributed under GPLv3**, with Stockfish's copyright notices preserved.

## Credits

### Testing & tuning

**Maurizio Platino** is the project's tester and search-tuner throughout its development. Beyond the
SPSA search-parameter tuning, he probes the engine's real playing strength by running it against
curated **hard positions at long time controls** — the kind of qualitative strength testing that fast
automated match-play cannot reach, and the project's only systematic testing of that sort — and has
generously contributed his hardware for the long tuning and testing runs. He also shares with the
author the cost of the cloud GPUs on which the project's networks are trained, Consilium included.
Triumviratus would be materially weaker without his work.

**Mark Tang** has tested the 8.0 prerelease against other engines and on the IQ4 tactical suite (results
in [`tests/`](tests/)). His remark that Stoofvlees answers very quickly between two moves, even at long time
controls, started the train of thought that led the author to the "surprise" rule of 8.0's time management: more
time on a move when the opponent did not play the reply the engine expected.

**Andres (EveAnn)** ran a 1,500-game gauntlet of the 11 October 8.0 pre-release against fifteen engines, Stockfish 19
included (results in [`tests/`](tests/)).

### Derived code

- **[Stockfish](https://github.com/official-stockfish/Stockfish)** (GPLv3) — the NNUE inference machinery in `nnue/` (accumulator stack, feature transformer, layers, threat and HalfKA features), ported from SFNNv16. Since then it has been extensively modified, tested and extended to fit Triumviratus' own networks: its own input blocks (PassedPawns, PawnPair), the four phase experts of the Consilium network, row permutation for cache locality, refresh caches for the pawn blocks and for phase changes, and many measured speed changes.
- **[BBC](https://github.com/maksimKorzh/chess_programming)** by Maksim Korzh ("Code Monkey King") — the original bitboard/magic-number move generator; the project's earliest (2024) foundation for `attacks.cpp`/`magic.cpp`/`movegen.cpp` and the first search, both since substantially rewritten and extended.
- **[Fathom](https://github.com/jdart1/Fathom)** (MIT) — Syzygy tablebase probing.

### Open-source engines studied

Ideas for search, move-ordering, time management and pruning were studied from — and in several cases
ported and then **re-tuned against the project's own data and network** — a number of open-source
engines. Credit and thanks to all of them:

- **[Stockfish](https://github.com/official-stockfish/Stockfish)** — the NNUE inference started from Stockfish's code (accumulators, features and the link with the search since largely rewritten for Consilium; forward pass and net loading still theirs, see [`source/nnue/README.md`](source/nnue/README.md)); the October 2026 restructuring of the search follows the structure of Stockfish 19's search (see [`DEVELOPMENT_8.0.md`](DEVELOPMENT_8.0.md), section 17).
- **[Reckless](https://github.com/codedeliveryservice/Reckless)** — quiet move-ordering (offense-square and king-shield-pawn terms), TT prefetch, capture-ordering ideas.
- **[Caissa](https://github.com/Witek902/Caissa)** — node-count move cache, quiescence capture history, moves-left time curve.
- **[Alexandria](https://github.com/PGG106/Alexandria)** — the multiplicative, stateless time-management factors.
- **[Pawnocchio](https://github.com/JonathanHallstrom/pawnocchio)**, **[Viridithas](https://github.com/cosmobobak/viridithas)** — the **`PawnPair` NNUE input feature** (see below).
- **[Berserk](https://github.com/jhonnold/berserk)**, **[Obsidian](https://github.com/gab8192/Obsidian)**, **[Ethereal](https://github.com/AndyGrant/Ethereal)** and **[Stormphrax](https://github.com/Ciekce/Stormphrax)** — assorted search, pruning and ordering refinements.

**NNUE input features — attribution.** The **`PawnPair`** block (pairs of pawns on the same or adjacent files, `4560` inputs) is **not an original idea of this project**. It was **invented by Jonathan Hallström for [Pawnocchio](https://github.com/JonathanHallstrom/pawnocchio)**, from his observation that in a network trained on *all* pawn pairs the ones that mattered were those at most one file apart. It was also used by **Stormphrax** and **Viridithas**, and in July 2026 **Stockfish adopted it too, as `PP_3Wide`**. Our C++ implementation (`nnue/features/pawn_pair.*`) and the trained weights are our own; the idea is his.

The **`PassedPawns`** block (one input per passed pawn, 96 slots) is, by contrast, **an original feature of this project** — designed, implemented and trained here, and present in no other engine we know of.

The open-source computer-chess community is what makes a project like this possible.

<sub>Developed openly and with significant AI assistance.</sub>