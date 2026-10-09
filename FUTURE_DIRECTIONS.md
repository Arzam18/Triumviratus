<div align="center">

<img src="logo.png" alt="Triumviratus" width="200">

# Triumviratus: future directions

**What the network gets wrong, how it divides the positions, and the proposals that follow**

**by Francesco Torsello**

<sub>in collaboration with Maurizio Platino</sub>

</div>

---

<div align="center">

[What the network misses](#1-what-the-network-misses) · [How the network divides positions](#2-how-the-network-divides-the-positions) ·
[Shared and private lanes](#3-shared-and-private-lanes-per-expert) · [Overlapping bands](#4-overlapping-material-bands-in-training) ·
[Positions we lose](#5-training-on-the-positions-we-lose) · [Graded surprise](#6-a-graded-surprise-rule) ·
[Status](#status)

</div>

---

Written on 9 October 2026, during the development of 8.0. The two studies below were made on the 8.0 network,
Consilium, with instrumentation added to the development source; their purpose is to decide the shape of the next network
before spending GPU time on it. The other sections are proposals, recorded so that they are not lost. The techniques
already in the engine, and what is new among them, are described in [`NOVELTIES.md`](NOVELTIES.md).

Both studies are first measurements on a few thousand positions. Each will be confirmed on a larger and more varied
set before any training is started.

## 1. What the network misses

**Method.** The search corrects the static evaluation where the network is wrong. For 3,888 positions taken from
about 7,000 games against many different engines, the score of a depth-1 search (network plus quiescence, so pending
captures are resolved) was compared with the score of a 300,000-node search. The difference, the residual, is what
the network does not see and the search finds. Around thirty chess concepts were then computed for each position
(our side minus the opponent's), and for each one the effect on the residual was estimated by least squares, holding
the starting score, the material balance and the number of pieces fixed. A concept with a strong and stable effect is
a candidate input for the next network, to be added as a zero-initialised graft, the method that produced the
`PassedPawns` block.

**Result.** Effect of one unit of the concept on the residual, in centipawns, with its t statistic (|t| > 3 is a
signal). A positive effect means the network underestimates the concept.

| concept | endgames (15 pieces or fewer) | middlegames (more than 15) |
|---|---:|---:|
| passed pawn, per rank of advance | **+11.1** (t 10.3) | +2.8 (t 2.8) |
| passed pawn, per pawn | **+31.7** (t 8.4) | +4.4 (t 1.3) |
| passed pawn the enemy king cannot catch | **+44.8** (t 6.2) | +15.5 (t 2.4) |
| connected passed pawns | **+23.0** (t 4.8) | **+20.1** (t 3.6) |
| space, per square | **+12.0** (t 6.6) | **+5.7** (t 6.0) |
| open files next to our own king | −12.2 (t −3.4) | **−15.4** (t −5.1) |
| enemy pieces attacking our king zone | −6.4 (t −1.9) | **−11.1** (t −5.2) |
| locked pawns, as an amplifier of the score (per 100 cp) | +6.5 (t 2.6) | **+11.2** (t 8.0) |
| opposite-coloured bishops, rook endgames (drawing factors) | no effect (t 0.7, −0.4) | |

**Reading.**

- **Passed pawns are still underestimated, mostly in endgames, even with the `PassedPawns` block.** The block encodes
  where a passer stands; what the residual points to is relational: whether the enemy king can catch it, whether it is
  connected, whether its path is free. A second version of the block with these relations is the first candidate.
- **The danger to the own king is underestimated in the middlegame:** open files next to the king and the number of
  enemy pieces attacking its zone. The threat inputs of the network describe attacks on pieces, not on the squares
  around the king. A king-zone block is the second candidate.
- **Space is underestimated in every phase**, by about 6 to 12 centipawns per square. Third candidate.
- The network handles the classical drawing factors well: opposite-coloured bishops and rook endgames leave no
  residual.

The residual also contains dynamic effects that a static input cannot capture, so these are candidates to be tested
by a graft, not conclusions.

## 2. How the network divides the positions

**Method.** Consilium chooses its expert by the number of pieces on the board. Here the network was asked which
regimes it sees itself. The outputs of its first layer (1,024 values per evaluation) were recorded with the position
they came from, 92,442 evaluations in all, and grouped without labels (principal components and k-means). The groups
were then compared with criteria that could choose the expert of a future network, by normalised mutual information
(0 = the criterion says nothing about the groups, 1 = it explains them completely).

| criterion | explains the network's groups (6 groups) |
|---|---:|
| **material band × queens on the board** | **0.43** |
| queens on the board (0, 1, 2) | 0.39 |
| material band (the criterion of Consilium) | 0.34 |
| number of pawns | 0.29 |
| rooks and minor pieces | 0.20 |
| closed pawn structure | 0.07 |
| opposite castling | 0.01 |

**Reading.** The presence of queens explains the network's own division of the positions better than the material
bands it was built with, and the two together explain it best. Inside each of the three lower bands the network still
separates positions with and without queens; inside the opening band it separates closed from open structures. The
largest group, 37% of the evaluations, is queenless positions with few pieces.

**Proposal: experts by material band and queens.** A next network with six to eight experts, chosen by material band
and by whether queens are on the board. Queens change only with a queen capture or a promotion, so the expert would
change as rarely as today and the incremental update would keep its cost. The choice would be measured on the network
rather than guessed.

## 3. Shared and private lanes per expert

The neuron study of [`NOVELTIES.md`](NOVELTIES.md) §5 found that every expert of Consilium uses nearly every pair of
the first layer, with different intensity: the endgame expert has 85 pairs it uses at least twice as often as the
others. A next network could give each expert a shared part of the first layer and a private part, wider where the
evaluation is harder. This is a mixture of experts with capacity where it is needed, at the same cost per position.

## 4. Overlapping material bands in training

When a capture moves a position from one expert to the next, the evaluation can jump. In training, each expert could
also learn from the positions of the neighbouring band, with a lower weight, so that neighbouring experts agree near
the boundary. The engine would not change at all. Earlier attempts to handle the boundary inside the search did not
gain; this addresses it where it arises, in the network.

## 5. Training on the positions we lose

Fine-tuning that gives more weight to positions like those where the engine loses against stronger opponents, the
"hard examples" of machine learning, rarely used for NNUE. A few thousand lost positions are not enough to move a
network trained on hundreds of billions; this needs a dedicated data generation that produces billions of such
positions, for example games against stronger engines, or positions where a deep search contradicts the static
evaluation (the residual of section 1, collected at scale).

## 6. A graded surprise rule

The surprise rule of 8.0 gives 20% more time when the opponent does not play the predicted reply. The graded form,
already in the development source and off by default (`TmSurpriseGrade`), weighs that extra time by how much the unexpected reply
changed the score against the expected one: no extra time when the opponent has given something away, up to twice the
extra when the reply was stronger than predicted. It will be measured at 40+0.4, where the surprise rule gained.

## Status

| direction | status |
|---|---|
| Inputs from the residual (section 1) | first measurement done; to be confirmed, then grafted one at a time |
| Experts by material and queens (section 2) | first measurement done; to be confirmed on a larger collection |
| Shared and private lanes (section 3) | proposal, needs a full training |
| Overlapping bands (section 4) | proposal, needs a full training |
| Positions we lose (section 5) | proposal, needs a dedicated data generation |
| Graded surprise (section 6) | implemented, off; game test queued |
