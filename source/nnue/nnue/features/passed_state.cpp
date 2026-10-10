/*
  PassedState (PassedPawns v3, 10/10/2026) - riferimento (radice, vg_check_acc, -DTRIUMV_VERIFY_GRAFT).
  GPLv3, derived from Stockfish NNUE plumbing (see COPYING).

  PASSED_STATE_SPEC (riferimento per il trainer: data_loader struct PassedState e passer_study/kit/verify_passedstate.py,
  che numerano le case da a1 = 0; qui a8 = 0, si traduce con sq ^ 56):
    passato(c, s) = PassedPawns v1.
    stop  = pezzo sulla casa davanti (bianco: s - 8, nero: s + 8 nel motore): 0 vuota, 1 proprio, 2 N/B nemico,
            3 R/Q nemico, 4 re nemico.
    prot  = pedone proprio su s + 8 +- 1 (bianco) / s - 8 +- 1 (nero), sulla scacchiera.
    conn  = un altro passato di c su una colonna adiacente.
    mu    = classe dei pezzi di ~c (3 donna, 2 torre, 1 minori, 0 nessuno); se 0 e Chebyshev(re di ~c, promozione) >
            mosse(c, s) allora 4. mosse = traverse alla promozione, 5 dalla seconda traversa.
    stato = stop + 5 * (prot + 2 * (conn + 2 * mu)); indice(p) = stato * 96 + PassedPawns::make_index(p, ksq, c, s).
*/

#include "passed_state.h"

#include <algorithm>
#include <cstdlib>

#include "../../bitboard.h"
#include "../../nn_board.h"

namespace Triumviratus::Eval::NNUE::Features {

int PassedState::entries_of(const unsigned long long* bb12, unsigned long long occ, std::uint16_t out[16]) {
    const Bitboard wp = bb12[NN_PAWN], bp = bb12[NN_PAWN + NN_BLACK];
    int            cls[2];
    for (int c = 0; c < 2; c++)
    {
        const int o = c * NN_BLACK;
        cls[c]      = material_class(bb12[NN_KNIGHT + o], bb12[NN_BISHOP + o], bb12[NN_ROOK + o], bb12[NN_QUEEN + o]);
    }
    int n = 0;
    for (int c = 0; c < 2; c++)
    {
        const Bitboard own = c == WHITE ? wp : bp;
        const Bitboard passers = PassedPawns::passers(Color(c), own, c == WHITE ? bp : wp);
        if (!passers)
            continue;
        const int oppCls = cls[1 - c];
        const int ek     = int(lsb(Bitboard(bb12[NN_KING + NN_BLACK * (1 - c)])));
        for (Bitboard b = passers; b;)
        {
            const int s = int(pop_lsb(b)), row = s >> 3, f = s & 7;
            // casa davanti: il bianco avanza verso la riga 0
            const int      stopSq = c == WHITE ? s - 8 : s + 8;
            const Bitboard sb     = 1ULL << stopSq;
            int            stop   = 0;
            if (occ & sb)
            {
                int pc = 0;
                while (!(bb12[pc] & sb))
                    pc++;
                const int pcColor = pc >= NN_BLACK, t = pc % NN_BLACK;
                stop = pcColor == c ? 1 : (t == NN_KNIGHT || t == NN_BISHOP) ? 2 : t == NN_KING ? 4 : 3;
            }
            // protetto: pedone proprio in diagonale dietro (bianco: riga + 1; nero: riga - 1)
            const int rb   = c == WHITE ? row + 1 : row - 1;
            int       prot = 0;
            if (rb >= 0 && rb <= 7)
                prot = int((f > 0 && (own >> (rb * 8 + f - 1) & 1)) || (f < 7 && (own >> (rb * 8 + f + 1) & 1)));
            const Bitboard file = 0x0101010101010101ULL << f;
            const Bitboard adj  = (f > 0 ? file >> 1 : 0) | (f < 7 ? file << 1 : 0);
            const int      conn = (adj & passers) != 0;
            int            mu   = oppCls;
            if (oppCls == 0)
            {
                const int promo = c == WHITE ? f : 56 + f;
                int       moves = c == WHITE ? row : 7 - row;
                if ((c == WHITE && row == 6) || (c == BLACK && row == 1))
                    moves = 5;
                const int kd = std::max(std::abs((ek >> 3) - (promo >> 3)), std::abs((ek & 7) - f));
                if (kd > moves)
                    mu = 4;
            }
            out[n++] = entry(c, s, state(stop, prot, conn, mu));
        }
    }
    return n;  // bianchi prima, case crescenti: ordinate come vuole il merge
}

void PassedState::append_active_indices(Color perspective, const NnBoard& pos, IndexList& active) {
    const int     ksq = pos.king(perspective);
    std::uint16_t e[16];
    const int     n = entries_of(pos.bbs(), pos.occ(), e);
    // le righe del blocco vanno dopo FeatRows + l'offset del blocco in pawn_grafts.h (qui: chi chiama aggiunge l'offset)
    for (int i = 0; i < n; i++)
        active.push_back(make_index(perspective, ksq, e[i]));
}

}  // namespace Triumviratus::Eval::NNUE::Features
