/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

//Definition of input features HalfKAv2_hm of NNUE evaluation function

#ifndef NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED
#define NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED

#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"
#include "feat_perm.h"

namespace Triumviratus::Eval::NNUE::Features {

// Feature HalfKAv2_hm: Combination of the position of own king and the
// position of pieces. Position mirrored such that king is always on e..h files.
class HalfKAv2_hm {

    // Unique number for each piece type on each square
    enum {
        PS_NONE     = 0,
        PS_W_PAWN   = 0,
        PS_B_PAWN   = 1 * SQUARE_NB,
        PS_W_KNIGHT = 2 * SQUARE_NB,
        PS_B_KNIGHT = 3 * SQUARE_NB,
        PS_W_BISHOP = 4 * SQUARE_NB,
        PS_B_BISHOP = 5 * SQUARE_NB,
        PS_W_ROOK   = 6 * SQUARE_NB,
        PS_B_ROOK   = 7 * SQUARE_NB,
        PS_W_QUEEN  = 8 * SQUARE_NB,
        PS_B_QUEEN  = 9 * SQUARE_NB,
        PS_KING     = 10 * SQUARE_NB,
        PS_NB       = 11 * SQUARE_NB
    };

    // Blocco del pezzo per prospettiva, indicizzato col codice del MOTORE (0..11 = P N B R Q K p n b r q k).
    // Convenzione: W = nostro, B = suo; dall'altra prospettiva si scambiano.
    static constexpr IndexType PieceSquareIndex[COLOR_NB][12] = {
      {PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING,
       PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING},
      {PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING,
       PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING}};

   public:
    // Triumviratus 28/09/2026: esperti per fase (TRIUMV_PSQ_PHASES > 1). Ogni fascia di materiale ha il suo blocco
    // di pesi HalfKA; l'indice e' quello di sempre + fase * BaseDimensions. Deve coincidere col trainer
    // (model/modules/features/halfka_v2_hm_phase.py e HalfKAv2_hm_P4 nel loader C++): hash, fasce, layout.
    static constexpr int Phases = TRIUMV_PSQ_PHASES;
    static_assert(Phases == 1 || Phases == 4, "fasce definite solo per 1 (HalfKA) e 4 (HalfKAv2_hm_P4)");

    // Hash value embedded in the evaluation file
    static constexpr u32 HashValue = Phases == 1 ? 0x7f234cb8u : (0x7f234cb8u ^ 0x50484134u);  // ^ "PHA4"

    // Number of feature dimensions
    static constexpr IndexType BaseDimensions =
      static_cast<IndexType>(SQUARE_NB) * static_cast<IndexType>(PS_NB) / 2;
    static constexpr IndexType Dimensions = BaseDimensions * Phases;

    // pezzi sulla scacchiera (re compresi) -> fascia, a frequenza uguale: <=9, 10-15, 16-23, >=24.
    static constexpr std::uint8_t PhaseOfPieceCount[33] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,        // 0..9
                                                           1, 1, 1, 1, 1, 1,                    // 10..15
                                                           2, 2, 2, 2, 2, 2, 2, 2,              // 16..23
                                                           3, 3, 3, 3, 3, 3, 3, 3, 3};          // 24..32
    static constexpr int phase_of_count(int pieceCount) {
        return Phases == 1 ? 0 : PhaseOfPieceCount[pieceCount];
    }
    static int phase_of(const DirtyPiece& dp) { return Phases == 1 ? 0 : dp.phase; }

#define B(v) (v * PS_NB)
    // clang-format off
    static constexpr IndexType KingBuckets[SQUARE_NB] = {
        B(28), B(29), B(30), B(31), B(31), B(30), B(29), B(28),
        B(24), B(25), B(26), B(27), B(27), B(26), B(25), B(24),
        B(20), B(21), B(22), B(23), B(23), B(22), B(21), B(20),
        B(16), B(17), B(18), B(19), B(19), B(18), B(17), B(16),
        B(12), B(13), B(14), B(15), B(15), B(14), B(13), B(12),
        B( 8), B( 9), B(10), B(11), B(11), B(10), B( 9), B( 8),
        B( 4), B( 5), B( 6), B( 7), B( 7), B( 6), B( 5), B( 4),
        B( 0), B( 1), B( 2), B( 3), B( 3), B( 2), B( 1), B( 0),
    };
    // clang-format on
#undef B
    // clang-format off
    // Orient a square according to perspective (rotates by 180 for black)
    static constexpr IndexType OrientTBL[SQUARE_NB] = {
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1 ,
    };
    // clang-format on

    // Maximum number of simultaneously active features.
    static constexpr IndexType MaxActiveDimensions = 32;
    using IndexList                                = ValueList<IndexType, MaxActiveDimensions>;
    using DiffType                                 = DirtyPiece;

    // Indice della feature (pezzo pc su s, nostro re su ksq), con case e codici del MOTORE (07/10/2026, scacchiera
    // unica v2). La rete numera a1 = 0: la sua casa e' la nostra ^ 56. Riflettere una casa cambia solo la traversa,
    // e OrientTBL e KingBuckets dipendono dalla sola colonna (OrientTBL) o sono indicizzati con la riflessione del
    // colore (KingBuckets[ksq ^ flip]): basta quindi che la riflessione del colore sia quella complementare, 56 per il
    // bianco e 0 per il nero, e l'indice e' identico a quello calcolato nella numerazione della rete.
    // phase = fascia di materiale (0 se TRIUMV_PSQ_PHASES == 1).
    static IndexType make_index(Color perspective, int s, int pc, int ksq, int phase = 0) {
        const IndexType flip = 56 * (1 - int(perspective));
        // `psq_row` e' l'identita' (permutazione per localita' provata e tolta, vedi feat_perm.h).
        return psq_row((IndexType(s) ^ OrientTBL[ksq] ^ flip) + PieceSquareIndex[perspective][pc]
                       + KingBuckets[ksq ^ flip])
             + IndexType(phase) * BaseDimensions;
    }

    // Get a list of indices for recently changed features. La fascia e' quella dello stato di arrivo; dentro una
    // catena incrementale e' costante (un cambio di fascia forza il refresh, vedi requires_refresh).
    static void append_changed_indices(Color            perspective,
                                       int              ksq,
                                       const DiffType&  diff,
                                       IndexList&       removed,
                                       IndexList&       added,
                                       int              phase = 0) {
        removed.push_back(make_index(perspective, diff.from, diff.pc, ksq, phase));
        if (diff.to != NN_SQ_NONE)
            added.push_back(make_index(perspective, diff.to, diff.pc, ksq, phase));
        if (diff.remove_sq != NN_SQ_NONE)
            removed.push_back(make_index(perspective, diff.remove_sq, diff.remove_pc, ksq, phase));
        if (diff.add_sq != NN_SQ_NONE)
            added.push_back(make_index(perspective, diff.add_sq, diff.add_pc, ksq, phase));
    }

    // Mossa del nostro re o cambio di fascia (una cattura che attraversa una soglia cambia TUTTE le righe HalfKA
    // attive, per entrambe le prospettive): l'accumulatore va ricostruito.
    static bool requires_refresh(const DiffType& diff, Color perspective) {
        return (Phases > 1 && diff.phaseChanged) || diff.pc == NN_KING + NN_BLACK * int(perspective);
    }
};

}  // namespace Triumviratus::Eval::NNUE::Features

#endif  // #ifndef NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED
