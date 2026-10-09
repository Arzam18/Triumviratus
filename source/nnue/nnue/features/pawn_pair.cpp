/*
  Pawn-pair input features (Triumviratus 6.0) — implementation.
  GPLv3, derived from Stockfish NNUE plumbing (see COPYING).
*/

#include "pawn_pair.h"

#include "feat_perm.h"

#include <array>

#include "../../bitboard.h"
#include "../../nn_board.h"

namespace Triumviratus::Eval::NNUE::Features {

// File band per square: same or adjacent files (Stormphrax kPpMasks). File
// distance is invariant under the orientation flips, so masks are on RAW squares
// (e la colonna e' la stessa nella nostra numerazione, a8 = 0: la tabella non cambia).
static constexpr auto PPBand = [] {
    std::array<Bitboard, SQUARE_NB> t{};
    for (int s = 0; s < SQUARE_NB; s++)
    {
        int      f = s & 7;
        Bitboard b = FileABB << f;
        if (f > 0)
            b |= FileABB << (f - 1);
        if (f < 7)
            b |= FileABB << (f + 1);
        t[s] = b;
    }
    return t;
}();

// Full refresh: all unordered in-band pairs among the (up to 16) pawns.
void PawnPair::append_active_indices(Color perspective, const NnBoard& pos, IndexList& active) {
    const int ksq = pos.king(perspective);

    // Era un doppio ciclo O(n^2) su fino a 16 pedoni (120 iterazioni) con un test di
    // distanza-colonna e un branch per coppia. Il percorso INCREMENTALE della stessa classe
    // (sotto) usava gia' `PPBand`, la banda precalcolata delle colonne adiacenti: qui si fa
    // lo stesso, e il numero di iterazioni scende ai soli partner effettivi.
    // Ogni coppia va emessa UNA volta sola: dopo `pop_lsb(bb)` il bitboard residuo contiene
    // per costruzione solo le case MAGGIORI di `s`, quindi basta intersecarlo con la banda —
    // niente maschere di confronto e niente rischio di doppioni.
    // `make_index` e' simmetrico (ordina i due pawn_id in hi/lo), quindi cambiare l'ordine di
    // enumerazione non cambia nessun indice: il refresh resta bit-identico.
    const Bitboard whitePawns = pos.pawns(WHITE);
    const Bitboard allPawns   = whitePawns | pos.pawns(BLACK);
#ifdef TRIUMV_X4_VLREG
    IndexList::Tail out(active);   // X4 VLREG: contatore in un registro (misc.h)
#else
    IndexList&      out = active;
#endif

    Bitboard bb = allPawns;
    while (bb)
    {
        const int s = pop_lsb(bb);
        const int c = (whitePawns >> s) & 1 ? WHITE : BLACK;

        Bitboard partners = bb & PPBand[s];
        while (partners)
        {
            const int p  = pop_lsb(partners);
            const int pc = (whitePawns >> p) & 1 ? WHITE : BLACK;
            out.push_back(feat_row(FoldOffset + make_index(perspective, ksq, s, c, p, pc)));
        }
    }
}

// Incremental: expand the pair-diff of a move from the DirtyPawns snapshot.
// Removed pairs are expanded against the BEFORE set (skipping already-processed
// removals so a removed-removed pair — e.g. en passant with both pawns in band —
// is emitted exactly once); the added pawn pairs against the AFTER set. A pair
// between a removed and an added pawn never coexisted, and both expansions get
// this right by construction (added not in BEFORE, removed not in AFTER).
void PawnPair::append_changed_indices(Color           perspective,
                                      int             ksq,
                                      const DiffType& diff,
                                      IndexList&      removed,
                                      IndexList&      added) {
    if (!diff.any)
        return;
#ifdef TRIUMV_X4_VLREG
    IndexList::Tail remOut(removed), addOut(added);   // X4 VLREG: contatori in registri (misc.h)
#else
    IndexList &     remOut = removed, &addOut = added;
#endif

    const Bitboard beforeAll = diff.before[WHITE] | diff.before[BLACK];

    for (int i = 0; i < diff.nRemoved; i++)
    {
        Bitboard partners = beforeAll & PPBand[diff.removedSq[i]] & ~(1ULL << diff.removedSq[i]);
        for (int j = 0; j < i; j++)
            partners &= ~(1ULL << diff.removedSq[j]);
        while (partners)
        {
            const int p  = pop_lsb(partners);
            const int pc = (diff.before[WHITE] >> p) & 1 ? WHITE : BLACK;
            remOut.push_back(feat_row(
              FoldOffset + make_index(perspective, ksq, diff.removedSq[i], diff.removedC[i], p, pc)));
        }
    }

    if (diff.addedSq != NN_SQ_NONE)
    {
        Bitboard afterW = diff.before[WHITE], afterB = diff.before[BLACK];
        for (int i = 0; i < diff.nRemoved; i++)
            (diff.removedC[i] == WHITE ? afterW : afterB) &= ~(1ULL << diff.removedSq[i]);

        Bitboard partners = (afterW | afterB) & PPBand[diff.addedSq];
        while (partners)
        {
            const int p  = pop_lsb(partners);
            const int pc = (afterW >> p) & 1 ? WHITE : BLACK;
            addOut.push_back(
              feat_row(FoldOffset + make_index(perspective, ksq, diff.addedSq, diff.addedC, p, pc)));
        }
    }
}

}  // namespace Triumviratus::Eval::NNUE::Features
