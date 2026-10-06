# Tests

Results from outside the project's own SPRT pipeline: matches and test suites run by other testers.
For Maurizio Platino's matches, the complete game records (PGN) and the GUI crosstables are in
**[Tors3/Triumviratus-Testing](https://github.com/Tors3/Triumviratus-Testing)**.

## Maurizio Platino — 7.0 against recent engines

Bullet matches against engines released in the last months, played by **Maurizio Platino** on his
own machine.

| Date | Opponent | Triumviratus build | Games | Score | Elo (Triumviratus) |
|---|---|---|---:|---:|---:|
| 2026-08-14 | pawnocchio 3.0-dev | 7.0 dev (2026-08-14) | 300 | 44.5% | −38 ± 15 |
| 2026-08-16 | PlentyChess 8.0.0 | 7.0 dev (2026-08-14) | 300 | 41.0% | −63 ± 16 |
| 2026-09-11 | Coda 0.9.4 | 7.0 (2026-09-10) | 300 | 44.5% | −38 ± 16 |
| 2026-09-14 | Caissa 1.26 | 7.0 (2026-09-10) | 300 | 52.8% | +20 |
| 2026-09-22 | Caissa 2.0 | 7.0 (2026-09-10) | 300 | 46.2% | −27 ± 15 |

<sub>Intel Core i7-8700 @ 3.20 GHz · Fritz 18 · 4 threads and 1024 MB hash per engine · ponder on ·
1 min + 1 s · openings `UHO_2024_8mvs_big_+110_+129.pgn` (Stefan Pohl, SPCC), 150 per match, each
played with both colours. ± is the 95% interval on the opening pairs (pentanomial). The Caissa 1.26
match has the crosstable only, without the games, hence no interval.</sub>

**What it says.** 7.0 is ahead of Caissa 1.26 and 25–65 Elo behind the newest releases: Caissa 2.0,
Coda 0.9.4, pawnocchio 3.0 and PlentyChess 8. These are the engines the 8.0 audit compares against
([`DEVELOPMENT_8.0.md`](../DEVELOPMENT_8.0.md)). Bullet with an unbalanced book widens the gaps
compared with a rating list, and the two August matches used a development build from a month
before the release.

## Maurizio Platino — ENET 2026 test suite

**ENET 2026** by Eduard Nemeth: 110 hard positions. Intel Core i7-8700 @ 3.20 GHz, Fritz 18, 8 GB hash.

| Triumviratus build | Solved |
|---|---:|
| 4.1 | 37 / 110 |
| 4.2 | 37 / 110 |
| 6.0 | 77 / 110 |
| 6.0, later build | 82 / 110 |
| 6.0 dev (2026-07-24) | 85 / 110 |
| 6.0 dev (2026-07-25) | 81 / 110 |
| 7.0 dev (2026-07-31) | 79 / 110 |
| 8.0 dev (2026-10-01) | 81 / 110 |
| **8.0 dev (2026-10-04)** | **89 / 110** |

The same suite on the same machine, other engines:

| Engine | Solved |
|---|---:|
| Stockfish 17.1 | 94 / 110 |
| Stockfish 19 | 91 / 110 |
| Stockfish dev (2026-09-13) | 91 / 110 |
| ShashChess 41 | 91 / 110 |
| Theoria 0.2 | 91 / 110 |
| Berserk 14 | 90 / 110 |
| **Triumviratus 8.0 dev (2026-10-04)** | **89 / 110** |
| Stockfish 18 | 82 / 110 |

From 6.0 to the 1 October build every version solved about three quarters of the suite. The build
of 4 October, the first with the restructured search, solves **89**: the best Triumviratus so far,
eight more than three days earlier, and within two positions of Stockfish 19.

## Mark Tang — 8.0 prerelease

Tests run by **Mark Tang** on the 8.0 prerelease build of 5 October 2026.

### Tactics: the IQ4 suite

**IQ4** is a set of 183 hard tactical positions from the IQ test series (Arasan's collection).

| Engine | Solved | |
|---|---:|---:|
| **Triumviratus 8.0 (prerelease)** | **145 / 183** | **79.2%** |
| Quanticade | 137 / 183 | 74.9% |
| Stockfish | 134 / 183 | 73.2% |

<sub>1 second per position · 1 thread · 64 MB hash · one engine process at a time. Triumviratus
had the highest score among the engines tested.</sub>

### Match against Stockfish dev

| Date | Opponent | Games | Result | Score | TC |
|---|---|---:|---:|---:|---|
| 2026-10-05 | Stockfish (development version) | 30 | +3 =17 −10 | 38.3% | 2 min + 1 s |

<sub>About −83 Elo; with 30 games the 95% interval runs from about −170 to −5.</sub>
