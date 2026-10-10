/*
  Pedoni passati con le loro relazioni (PassedPawns v2, 09/10/2026) - implementazione.
  GPLv3, derived from Stockfish NNUE plumbing (see COPYING).

  PASSED_REL_SPEC (riferimento per il trainer, case in numerazione standard a1 = 0 del trainer: si traduce con sq ^ 56):
    passato(c, s): nessun pedone avversario davanti sulla colonna di s o sulle adiacenti, nessun pedone proprio davanti
                   sulla stessa colonna (= PassedPawns v1).
    mosse(c, s)   = traverse che mancano alla promozione; 5 se il pedone e' sulla sua seconda traversa (passo doppio).
    imprendibile  = distanza di re (Chebyshev) fra il re nemico e la casa di promozione > mosse(c, s).
    collegato     = un altro passato di c su una colonna adiacente (qualsiasi traversa).
    libero        = nessun pezzo (di nessun colore) sulle case davanti a s fino alla promozione compresa.
    stato = imprendibile | collegato << 1 | libero << 2.
    indice(prospettiva p) = (c != p ? 384 : 0) + stato * 48 + casa orientata - 8, orientazione come PassedPawns.
*/

#include "passed_rel.h"

#include "passed_pawns.h"

#include <algorithm>
#include <cstdlib>

#include "../../bitboard.h"
#include "../../nn_board.h"

namespace Triumviratus::Eval::NNUE::Features {

int PassedRel::entries_of(const unsigned long long* bb12, unsigned long long occ, std::uint16_t out[16]) {
    const Bitboard wp = bb12[NN_PAWN], bp = bb12[NN_PAWN + NN_BLACK];
    int            n  = 0;
    for (int c = 0; c < 2; c++)
    {
        const Bitboard passers = PassedPawns::passers(Color(c), c == WHITE ? wp : bp, c == WHITE ? bp : wp);
        if (!passers)
            continue;
        const int ek = int(lsb(Bitboard(bb12[NN_KING + NN_BLACK * (1 - c)])));
        for (Bitboard b = passers; b;)
        {
            const int s = int(pop_lsb(b)), row = s >> 3, f = s & 7;
            const Bitboard file = 0x0101010101010101ULL << f;
            // il bianco promuove alla riga 0 (case a8 = 0), il nero alla riga 7
            const int promo = c == WHITE ? f : 56 + f;
            int       moves = c == WHITE ? row : 7 - row;
            if ((c == WHITE && row == 6) || (c == BLACK && row == 1))
                moves = 5;
            const int kd = std::max(std::abs((ek >> 3) - (promo >> 3)), std::abs((ek & 7) - f));
            const Bitboard ahead = c == WHITE ? file & ((1ULL << (row * 8)) - 1) : file & ~((1ULL << ((row + 1) * 8)) - 1);
            const Bitboard adj   = (f > 0 ? file >> 1 : 0) | (f < 7 ? file << 1 : 0);
            const int state = int(kd > moves) | int((adj & passers) != 0) << 1 | int((ahead & occ) == 0) << 2;
            out[n++] = std::uint16_t(c << 9 | state << 6 | s);
        }
    }
    return n;
}

void PassedRel::append_active_indices(Color perspective, const NnBoard& pos, IndexList& active) {
    const int     ksq = pos.king(perspective);
    std::uint16_t e[16];
    const int     n = entries_of(pos.bbs(), pos.occ(), e);
    for (int i = 0; i < n; i++)
        active.push_back(FoldOffset + make_index(perspective, ksq, e[i] >> 9, (e[i] >> 6) & 7, e[i] & 63));
}

// L'incrementale (09/10/2026 sera) e' nel meccanismo comune dei blocchi da innesto: pawn_grafts.cpp.

}  // namespace Triumviratus::Eval::NNUE::Features
