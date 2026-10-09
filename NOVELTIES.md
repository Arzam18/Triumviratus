<div align="center">

<img src="logo.png" alt="Triumviratus" width="200">

# Triumviratus: what is new

**A register of the techniques of Triumviratus, from version 1.0 to 8.0, compared with the published work of other
engines**

**by Francesco Torsello**

<sub>in collaboration with Maurizio Platino</sub>

</div>

---

<div align="center">

[Method](#method) · [Summary](#summary) · [Consilium](#1-consilium-experts-by-material-on-the-first-layer) ·
[PassedPawns](#2-the-passedpawns-input-block) · [CorrPhase](#3-correction-history-per-expert-corrphase) ·
[Move ordering](#4-move-ordering-with-chess-knowledge) · [Neurons](#5-the-first-layer-neuron-by-neuron-per-expert) ·
[Other new ideas](#6-other-new-ideas-not-adopted) · [Revisited](#7-known-ideas-revisited-in-a-different-form) ·
[Speed](#8-speed-work) · [Done elsewhere first](#9-done-elsewhere-first) · [Open checks](#10-open-checks)

</div>

---

## Method

Written on 9 October 2026, during the development of 8.0. Every technique of the project that looked new, or new in
its form, was compared with other engines in two ways: by reading the source code of nine engines (Stockfish 19,
Reckless, PlentyChess 8.0, Viridithas 20, Berserk 14, Caissa 2.0, Coda, Integral 8, Stormphrax 8.0) and by searching
chessprogramming.org, TalkChess, GitHub and the literature. A search that finds nothing does not prove that nobody
has done it: the developer channels on Discord are not indexed, and closed-source engines cannot be read. Where no
precedent was found, the text says "no earlier example found in the sources checked", and every claim of priority
below is meant **to our knowledge**.

Each entry is in one of three categories:

- **New**: no earlier example found in the sources checked.
- **Revisited**: a known idea, taken up again in a different form or at a different scale.
- **Done elsewhere first**: the idea was published by another engine before us, even when we reached it on our own;
  the source is named. The category describes public priority, not where our idea came from.

Measurements are Elo differences from SPRT or fixed-games runs on the project's test machine, with the 95% error
margin and the number of games; the full context of each is in [`DEVELOPMENT_8.0.md`](DEVELOPMENT_8.0.md),
[`NETWORKS.md`](NETWORKS.md) and the [`archive`](archive) of earlier versions.

## Summary

Of 43 entries, **8 are new**, **20 are known ideas revisited in a different form**, and **15 were done elsewhere
first**. Three of the new ones are adopted and measured as gains, and together they form the core of 8.0: a network
divided by game phase, an input block that the king-relative block cannot express, and corrections that follow the
experts of the network.

| entry | category | version | measured effect | status |
|---|---|---|---|---|
| Consilium: four experts by material on the king-relative input block | New in its form | 8.0 | 8.0 against 7.0 **+27.3 ± 8.3** (15+0.15, 2,000 games), of which about +15 from the network with its tuning | adopted, the 8.0 network |
| `PassedPawns` input block (96 inputs) | New | 6.0 | **+6.96 ± 6.56** (15+0.15, 2,596), network alone | in every network since 6.0 |
| Correction history per expert (`CorrPhase`) | New | 8.0 | **+3.2 ± 3.8** (12+0.12, 8,634) | adopted |
| Knight outposts in move ordering (`OutpostOrder`) | New | 8.0 | +0.21 ± 3.93 (10+0.1, 8,244) at a hand-set value | in the running SPSA |
| Capture of a bishop of the pair in move ordering (`BishopPairCapt`) | New | 8.0 | not measured alone | in the running SPSA |
| Expert-boundary ideas (PhaseEdge) | New | 8.0 | from +1.3 ± 2.3 (24,292) to −14.1 | closed |
| Correction from the opponent's two last moves (`CorrContOppW`) | New | 8.0 | −4.8 ± 8.7 (1,806); real signal, correlation +0.24 | off, in a tuning queue |
| `Mobility` input block | New as an idea | 8.0 dev | cost only: −16.2% NPS | archived, never trained |
| Surprise rule (`TmSurpriseScale`) | Revisited | 8.0 | **+6.23 ± 6.75** at 40+0.4 (2,174); neutral at 16+0.16 | adopted |
| Guard on the low-depth hash-move extension (`LdseMax`) | Revisited | 8.0 | **+6.25 ± 7.89** (15+0.15, 2,112) | adopted |
| Passed-pawn pushes in endgames reduced less, never pruned (`PassedPushRed`) | Revisited | 8.0 | +4.4 ± 6.8 on an endgame book | adopted |
| Passed-pawn push bonus in move ordering (`PassedPush`) | Revisited | 8.0 | not measured alone | in the running SPSA |
| Neuron study of the first layer, per expert | Revisited | 8.0 | no dead or duplicate neurons; experts specialise by intensity | study |
| Universal executable from one unit per source file | Revisited | 8.0 | −0.28% / −0.17% cycles per node against the separate build | adopted |
| Refresh cache of the pawn input blocks keyed by the pawn bitboards | Revisited | 7.0 | +2.8% NPS (AVX2), identical tree | adopted |
| `tdperft` and `nnperft` | Revisited | 8.0 | 0 errors on 482 M nodes and on the Chess960 suite | permanent tools |
| Zero-initialised graft of a new block on a finished network, base frozen | Revisited | 6.0 | `PassedPawns` +7 Elo in about 4 epochs | in use |
| Analysis mode compiled as a second copy of the search | Revisited | 8.0 | more test positions solved; nothing changes in games | adopted |
| Fine-tuning one expert at a time | Revisited | 8.0 | +0.7 ± 6.1 (15+0.15, 3,302) | not kept |
| Material-key correction table | Revisited | 6.0 | +10.43 ± 5.57 at 10+0.1, −6.89 ± 9.01 at 20+0.2 | removed in 7.0 |
| `CorrUncert`, disagreement between correction tables as uncertainty | Revisited | 6.0 dev | −0.74 ± 7.29 (1,878) | closed |
| `TroubleMaking`, the hardest move for the opponent in a lost position | Revisited | 6.0 dev | −2.67 ± 10.66 (910) | suspended |
| `BrilliantSac`, extension of losing captures with high capture history | Revisited | 6.0 dev | +2.70 ± 7.25 (2,190); −17.83 ± 15.66 (390) | closed |
| SPLE, lazy evaluation with the parent's positional part | Revisited | 6.0 dev | offline error of the order of the margins | closed |
| `TMEvalDisagree`, more time when search and static evaluation diverge | Revisited | 7.0 dev | neutral, ±1.8 on about 40,000 games | closed |
| Policy network as a position signal (`PolicySeed`, `EntropyTM`, `PolicyEasyMove`, `PolicyRootLmr`) | Revisited | 3.6-3.8, August 2026 | −35.17, −16.7, −14.35, +1.36 ± 2.71 | closed |
| `TalStyle`, root switch to verified sacrifices | Revisited | 8.0 dev | −40 ± 18 (388) | removed |
| Move-ordering rank by mask and popcount, 16 moves at a time | Revisited | 8.0 | −0.70% cycles per node (AVX-512) | adopted |
| Policy network inside the alpha-beta search | Elsewhere first | 3.1 | −85, −30.56 ± 21.9, −46.89, −22.83 | closed |
| `DiverseSMP`, LMR bias per helper thread | Elsewhere first (Reckless) | 3.5, 6.0 | +4.91 ± 7.97 at 8 threads (1,700) | in 6.0 and 7.0 |
| TMv2 multiplicative time manager | Elsewhere first (Alexandria, Caissa) | 6.0 | +23.8 ± 18.2 at 20+0.2 (380) | in 6.0 and 7.0 |
| `PawnPair` input block | Elsewhere first (Pawnocchio) | 6.0 | +18.27 ± 9.94 (1,104), with self-play data | in every network since 6.0 |
| Less time when behind on the clock (`TmBehindMul`) | Elsewhere first (Stockfish after 19) | 8.0 | **+5.23 ± 4.72** at 6+0.06 (6,378) | adopted |
| Low-depth hash-move extension (LDSE) | Elsewhere first (Reckless, Stormphrax) | 8.0 | **+4.5 ± 3.3** (10+0.1, 11,604) | adopted |
| King-shield malus in move ordering (`KingShield`) | Elsewhere first (inspired by Reckless) | 6.0, 8.0 | +3.53 ± 3.42 (6.0); −1.57 ± 4.32 (8.0) | in the running SPSA |
| Attack from a safe square in move ordering (`AttackOrder`) | Elsewhere first (Reckless), our target rule | 8.0 | +0.39 ± 5.42 with the outposts (4,428) | in the running SPSA |
| Queen promotion in move ordering (`PromoOrder`) | Elsewhere first (Reckless) | 8.0 | not measured alone | in the running SPSA |
| Advanced pawn pushes exempt from pruning (`PasserGuard`, `PasserLmr`) | Elsewhere first (Stockfish 8) | 7.0 dev | −9.04 ± 7.49; +0.30 ± 2.56 (22,000) | closed |
| Fifty-move band in the hash key and in the corrections | Elsewhere first (Reckless) | 8.0 | `CorrFiftyStep` +1.25 ± 2.87 | closed |
| Deterministic PGO training at fixed nodes | Elsewhere first (Stockfish) | 8.0 | repeatable speed measurements | adopted |
| Printed scale, +1.00 = 50% win | Elsewhere first (Stockfish 15.1) | 5.1 | display only | adopted |
| First own network, `rubicon-v1` | Elsewhere first | 4.2 | −38.91 ± 19.94 against 4.1 with a Stockfish network | replaced in 5.0 |
| Chess960 | Elsewhere first | 8.0 | no speed cost | adopted |

---

## 1. Consilium: experts by material on the first layer

**What it is.** The king-relative input block `HalfKAv2_hm` of the SFNNv16 network is replaced by four sets of
weights, one per material band (pieces on the board, kings and pawns included: up to 9, 10-15, 16-23, 24 or more).
The thresholds are quartiles of the training corpus, so each expert sees about a quarter of the positions. One expert
is active per position, so an evaluation costs almost the same as with a single block: −7.5% NPS, against −11.2% for
a first layer of 1,536 and −20% for 2,048. In the trainer each expert is a shared base plus a per-band delta that
starts at zero: the network begins as an ordinary HalfKA network and the experts separate only where the data asks
for it. In the engine, a capture that crosses a threshold forces a refresh, and the refresh cache is kept per band.
The idea is the author's, taken from mixture-of-experts language models (many parameters, few active per token).

**Measured.** Trained on 28-29 September 2026 (374 epochs of pre-training and two fine-tunes, about 472 billion
positions). Version 8.0 against the 7.0 release: **+27.3 ± 8.3 Elo** at 15+0.15 on 2,000 games, of which about +15
come from the network with its re-tuned parameters ([`NETWORKS.md`](NETWORKS.md)).

**Compared with others.** Choosing weights by piece count is common, but after the accumulator: Stockfish's layer
stacks and the output buckets of many engines select the output layers by piece count. Input buckets of the first
layer are chosen by the king square, not by material; no engine with input buckets by material or phase was found.
Stockfish 16.1 switched between two whole networks by material imbalance, which routes between networks rather than
between experts inside one layer. Outside NNUE, M2CTS used phase experts with AlphaZero-style MCTS
([arXiv 2401.16852](https://arxiv.org/abs/2401.16852)). Mark Tang had tried an unpublished network of phase experts
in early 2026 that ran faster but did not play stronger (independent work, see [`NETWORKS.md`](NETWORKS.md)).

**Category.** New in its form: experts by material band on the king-relative block of the first layer, one active per
position, trained as base plus delta, in a top engine and with a measured gain. Mixtures of experts and phase
specialisation exist in other fields, so the general idea is not.

## 2. The PassedPawns input block

**What it is.** 96 inputs, one per passed pawn: 48 oriented squares for each side. A pawn is passed when no enemy pawn
stands ahead of it on its file or the adjacent ones and no own pawn stands directly ahead. This is a relation across
three files that the king-relative block does not express directly. The block deliberately encodes only the square:
blocked, supported and connected passers can be learned by the pairwise product of the first layer. In the engine it
is added to the threat accumulator and reuses the incremental update of pawn events.

**Measured.** Grafted at zero on `rubicon-alea-v2` and trained alone with the base frozen, about 4 epochs:
**+6.96 ± 6.56** on 2,596 games at 15+0.15, network alone (17 July 2026). It has been in every network since:
`legio-septima` (7.0) and Consilium (8.0).

**Compared with others.** Stockfish's `PP_3Wide` encodes pairs of pawns on nearby files, not the passed state. None of
the nine engines read has passed-pawn inputs. The only precedent found is an unimplemented note in Viridithas's ideas
file ("passed pawns by file?").

**Category.** New: to our knowledge the first passed-pawn input block implemented and measured.

## 3. Correction history per expert (CorrPhase)

**What it is.** The keys of the pawn and minor-piece correction tables are mixed with the material band, that is with
the expert of the network: each expert learns its own error. A first test (−5.1 ± 8.1) was distorted by a prefetch
that used the unmixed keys; an analysis made before closing the idea found it.

**Measured.** **+3.2 ± 3.8** on 8,634 games at 12+0.12, adopted on 6 October 2026. A diagnostic build shows that the
corrections remove 58.8% of the evaluation error with `CorrPhase` against 57.3% without, the gain coming from
endgames with nine pieces or fewer.

**Compared with others.** Correction history began in Caissa (October 2023) and is indexed today by pawn structure,
minor pieces, non-pawn material and previous moves. Reckless splits its tables by the fifty-move counter. No engine
was found that splits the corrections by material band or by network expert.

**Category.** New, and tied to Consilium.

## 4. Move ordering with chess knowledge

Written on 8 October 2026, off by default, now in an SPSA of 46 search parameters at 20+0.2. At hand-set values no
term moves the result, because an ordering bonus acts together with the histories and the thresholds that separate
good quiet moves from bad ones ([`DEVELOPMENT_8.0.md`](DEVELOPMENT_8.0.md) §34). The SPSA will decide their values;
an SPRT of the final vector against the defaults will decide whether they stay.

- **Knight outposts (`OutpostOrder`, `OutpostSafe`), the author's idea.** A bonus for a knight move to a square on
  ranks 4-6 defended by an own pawn and out of reach of every enemy pawn, now and later; with `OutpostSafe` also
  without an enemy bishop of the square's colour. +0.21 ± 3.93 on 8,244 games at a hand-set value. Outposts are a
  classical evaluation term, but none of the nine engines read uses them in move ordering, and no example was found on
  the web. **New.** The bishop variant (`OutpostOrderB`) uses the same mask and is not presented as new.
- **Capture of a bishop of the pair (`BishopPairCapt`).** A bonus in the tactical score for capturing a bishop when
  the opponent holds the pair. No example in the move pickers read. **New.**
- **Passed-pawn push (`PassedPush`).** A bonus for pushing a passed pawn from a given rank to the seventh, onto a
  square where it does not lose material. Special treatment of passer pushes is classical (extensions, seventh-rank
  pushes in quiescence); as an ordering bonus no example was found. **Revisited.**

The king-shield, safe-attack and promotion terms of the same batch come from Reckless and are listed in
[section 9](#9-done-elsewhere-first).

## 5. The first layer neuron by neuron, per expert

**What it is.** A build with `-DTRIUMV_NEURONS` records, for each evaluation of the search, the outputs of the feature
transformer (clipped ReLU and pairwise product, 512 pairs per perspective) separately for each material band, that
is for each expert, together with a sample of whole output vectors annotated with the piece count and the material
difference. Two questions: are there dead, saturated or duplicate neurons (wasted work, so speed at equal strength
with a narrower layer), and does the network build its own material counters (which bears on the division by
material). The starting point was a neuron-by-neuron analysis of a Stockfish network shared in the Stockfish
community; a separate analysis per expert needs a network with experts.

**Measured (9 October 2026).** 12,000 positions, 3,000 per band, from about 7,000 games against many different engines
and from varied openings, 500,000 nodes each: **2.58 billion evaluations**. A first run on 300 positions per band had
given the same picture.

| band (expert) | dead (< 0.1%) | rare (< 1%) | saturated | mean activity | duplicates (r > 0.95) |
|---|---:|---:|---:|---:|---:|
| up to 9 pieces | 0 | 0 | 0 | 15.5% | 0 |
| 10-15 | 0 | 0 | 0 | 14.5% | 0 |
| 16-23 | 0 | 0 | 0 | 14.0% | 0 |
| 24 or more | 0 | 14 | 0 | 12.9% | 0 |

- **Nothing to prune.** No pair is dead in every band, none is saturated, none duplicates another. Consilium uses all
  512 pairs, so removing neurons would not make it faster.
- **The experts specialise by intensity, not by selection.** Every expert switches on nearly every pair (overlap 0.97
  to 1.00 between bands), but how often differs: 306 pairs change their activation frequency by at least a factor of
  2 between the most and the least active band, 102 by a factor of 4, 33 by a factor of 10 (for example a pair active
  in 26% of endgame evaluations and almost never in the opening). The endgame expert has 85 "preferred" pairs
  (activity at least twice the mean of the other bands), the opening expert 29. Neighbouring bands have similar
  activity profiles (correlation 0.85-0.92), the extremes do not (0.43).
- **Material counters in fixed places.** A few pairs follow the material difference in every expert (|r| up to 0.83),
  in the same positions of the layer: consistent with training as a shared base plus deltas.

The specialisation by intensity is the measured fact that could justify a different width per expert in the next
network.

**Category.** Revisited: a known analysis technique, applied for the first time, to our knowledge, to a network
divided by phase.

## 6. Other new ideas, not adopted

- **Expert-boundary ideas (PhaseEdge), 8.0.** A capture that changes the active expert must clear an extra margin
  (−2.5 ± 10.0, removed); "improving" is not computed between evaluations of different experts (−14.1 ± 19.4,
  removed); a capture that crosses a boundary is neither pruned nor reduced more (+1.3 ± 2.3 on 24,292, neutral, off).
  These ideas cannot exist without a network with experts. **New**, none adopted.
- **Correction from the opponent's plan (`CorrContOppW`), 8.0, the author's idea.** Next to the continuation pairs
  built on our own moves, the pair of the opponent's two last moves. The cells carry information (correlation +0.24
  with the residual error), but −5.2 ± 12.7 (742) at the existing weight and −4.8 ± 8.7 (1,806) at the regression
  weight. Stockfish and Reckless use only pairs with our moves. **New**, off, waiting for a tuning run.
- **`Mobility` input block, 8.0 development.** One feature per knight, bishop, rook and queen (oriented square and four
  mobility buckets, 2,048 inputs). Only its cost was measured, with random weights: −16.2% NPS, because mobility
  changes with every move. Archived on 29 September 2026 without training. No network of the nine engines has
  mobility inputs. **New as an idea**, with no measurement of strength.

## 7. Known ideas revisited in a different form

These entries take a known idea and change its form, its scale or its use. Those in use:

- **Surprise rule, 8.0, the author's idea.** If the opponent does not play the reply our principal variation
  predicted, the time for the move is multiplied by 1.2, so the time that the stability rule saves on predicted
  replies goes to uncertain moves. The starting point was a remark by Mark Tang: Stoofvlees answers very quickly
  between two moves, even at long time controls. **+6.23 ± 6.75 at 40+0.4** (2,174), neutral at 16+0.16; after a
  surprise the engine spends 12% more time and searches 0.35-0.4 plies deeper. A factor on the predicted reply
  exists before: Caissa publishes it in both directions (0.915 on the predicted reply, 1.132 otherwise), and our own
  6.0 and 7.0 used it as a declared port from Caissa ([`archive/DEVELOPMENT_6.0.md`](archive/DEVELOPMENT_6.0.md),
  "predicted-move factors"). The 8.0 form keeps only the surprise direction, applied on top of the time manager
  rewritten on 4 October, and the measured dependence on the time control is ours.
- **Guard on LDSE (`LdseMax`).** At most one low-depth hash-move extension per line, from a measurement of the tree
  (rook endgames ×5 to ×16): +6.25 ± 7.89 at 15+0.15. Neither Reckless nor Stormphrax caps it per line; limiting
  extensions along a line is known in general.
- **Passed-pawn pushes in endgames (`PassedPushRed`).** With little material, a push to the sixth or seventh rank is
  reduced less and never pruned, because the network sees passers through `PassedPawns`: +4.4 ± 6.8 on an endgame
  book. Stockfish 8 exempted advanced pawn pushes from shallow pruning.
- **Refresh cache of the pawn blocks, 7.0.** `PawnPair` and `PassedPawns` depend only on the pawn bitboards and the
  orientation, and refreshes come from king moves, which leave the pawns alone; their contribution is kept with the
  full pawn bitboards as key (no collision possible). +2.8% NPS on AVX2 with an identical tree. The principle of the
  pawn hash table of classical evaluations, applied to an accumulator; Stockfish 19 has no such cache.
- **Universal executable, 8.0.** Five variants (AVX2 without PEXT, AVX2, AVX-512, VNNI-512, AVX-512 ICL), chosen at
  start-up. Each source file is compiled once per variant, the variants are joined in the final link with ThinLTO, and
  the five profile-guided profiles are merged because ThinLTO refuses modules with conflicting profiles. −0.28% and
  −0.17% cycles per node against the separate build, where one unit per variant had cost about 1%. Stockfish has
  had a universal binary since June 2026, built differently, with per-architecture LTO and renamed sections, a route
  not available with clang-cl and lld-link on Windows.
- **`tdperft` and `nnperft`.** A perft that uses the search's own move generation, make and unmake, and at every node
  recomputes from scratch every hash key, the occupancy and the piece table, and checks the pseudo-legality test used
  for hash and killer moves; and a perft that compares the incremental accumulator with a full refresh at every node.
  They validated the correctness audit (482 million nodes), Chess960 and the single board.
- **Zero-initialised graft with the base frozen, 6.0.** A new block is added at zero to a finished network (identical
  output at the start, checked with the bench) and trained alone: enough for `PassedPawns` (+7 Elo in about 4 epochs)
  and a cheap way to screen input features. Function-preserving initialisation is known in machine learning; as a
  screening method for NNUE features no description was found.
- **Analysis mode, 8.0.** With `UCI_AnalyseMode` or `go infinite` the search prunes and reduces less at depth; the
  search is compiled twice, a game copy with every analysis term a compile-time constant and an analysis copy, so
  games are unaffected.
- **Move-ordering rank by mask and popcount, 8.0.** The rank of each move is found by comparing it with 16 others at a
  time and counting with a popcount (−0.70% cycles per node on AVX-512). Stockfish uses a vectorised insertion sort.

Those tried and closed, each a variant of a known idea: the material-key correction table, `CorrUncert` (the
disagreement between correction tables as uncertainty; Stockfish uses the size of the total correction),
`TroubleMaking` (in a lost position, the move that cost the search almost as many nodes as the best, hence hard to
refute; self-play cannot measure it, because a copy of the engine finds the refutation as easily as it does),
`BrilliantSac`, SPLE, `TMEvalDisagree` (the nearest precedent is Stockfish 15.1's `complexPosition`), fine-tuning one
expert at a time, `TalStyle` (ShashChess has style personalities acting on thresholds and evaluation; the root switch
to verified sacrifices was not found), and the uses of the policy network as a signal on the position
(`PolicySeed`, `EntropyTM`, `PolicyEasyMove`, `PolicyRootLmr`).

## 8. Speed work

Most of the speed of 8.0 comes from engineering on techniques that other engines also use, and we say so. Checked
against the sources: the early prefetch of the child's hash entry from an estimated key, the prefetch of the
correction entries, the AVX-512 compaction of the move list, the vectorised slider attacks and the large pages for the
search state are all in Stockfish; keeping the minor and major piece keys incrementally follows Stockfish; finding the next attacker of the
static exchange evaluation from a bitboard is what Reckless does. These are **done elsewhere first**, written in our
code and measured one by one.

What is ours in this area is listed in [section 7](#7-known-ideas-revisited-in-a-different-form): the refresh cache of
the pawn blocks, the universal build from one unit per source file, the move-ordering rank by popcount, and the
analysis mode compiled as a second copy. Some further patches (the compaction of the threat features, the passed-pawn fill, the prefetch of the threat rows of the network) have not yet been checked line
by line against every engine and are listed among the [open checks](#10-open-checks). The measurement method, with
the engine pinned to one processor and only the search threads counted, is described in
[`DEVELOPMENT_8.0.md`](DEVELOPMENT_8.0.md) §36.

## 9. Done elsewhere first

Ideas we use or tried that other engines published first. Several we reached on our own; the category records public
priority.

- **Low-depth hash-move extension (LDSE).** +4.5 ± 3.3 on 11,604 games, adopted. From Reckless and Stormphrax, with
  our form and values.
- **Less time when behind on the clock (`TmBehindMul`).** The author's idea, +5.23 ± 4.72 on 6,378 games at 6+0.06.
  Stockfish has an almost identical rule in a development commit after version 19 (`5ca4bfe7`, "Decrease time usage
  if engine has time disadvantage").
- **King shield in move ordering.** Inspired by the malus of Reckless ("Resist moving king wall pawns"), ported in 6.0
  (+3.53 ± 3.42) and written again for 8.0 with a condition on the opponent's material.
- **Attack from a safe square, queen promotion in move ordering.** From Reckless; the target rule (an enemy piece,
  neither pawn nor king, of a higher class or undefended) is ours.
- **Policy network inside alpha-beta (3.1 "Hybrid", and again in August 2026).** A move-predicting network used for
  ordering and reductions. Every form lost: −85, −30.56 ± 21.9, −46.89 ± 33.99, −22.83 ± 8.20. Neural move ordering in
  alpha-beta goes back to Kocsis, Uiterwijk and van den Herik (2001) and to the Neural MoveMap heuristic. The lesson
  recorded: ordering already reaches 79.2% first-move cutoffs without a hash move, and a static prior computed at the
  parent loses against histories that adapt to the subtree.
- **`DiverseSMP`.** A small LMR bias per helper thread of Lazy SMP; the value comes from Reckless, which published it
  six weeks before our first form.
- **TMv2**, the 6.0 time manager, composed of declared rules from Alexandria and Caissa.
- **`PawnPair`**, the idea of Jonathan Hallström for Pawnocchio, later adopted by Stockfish as `PP_3Wide`.
- **`PasserGuard` and `PasserLmr`**, in Stockfish 8 as the exemption of advanced pawn pushes.
- **Fifty-move bands** in the hash key and in the corrections, from Reckless.
- **Deterministic profile training**, as Stockfish trains its PGO with the fixed-depth bench.
- **The printed scale** (+1.00 = 50% win), as Stockfish since 15.1.
- **`rubicon-v1`**, the first network trained by the project (4.2), on the official recipes and trainer.
- **Chess960** support.

Versions 1.x and 2.x were learning engines, built on the models of VICE and of BBC (the video series of Code Monkey
King), and have no new entries.

## 10. Open checks

1. **Consilium and recent engines.** No engine with input buckets by material was found, but the developer channels
   are not indexed. Every statement of priority here is meant to our knowledge; corrections are welcome.
2. **Knight outposts and the bishop-pair capture in move ordering.** Nine engines and the web were checked; Obsidian,
   Koivisto, Ethereal and recent Berserk were not read line by line.
3. **Speed patches.** Threat-feature compaction, the passed-pawn fill and the threat-row prefetch, to be compared with
   every engine before any claim.
4. **Surprise rule in closed engines.** It may exist in commercial or closed engines that cannot be read.
5. **Zero-initialised graft.** Other developers probably use it without describing it.
