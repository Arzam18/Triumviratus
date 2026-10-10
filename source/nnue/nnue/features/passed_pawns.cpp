/*
  Passed-pawn input features (Triumviratus) — implementation.
  GPLv3, derived from Stockfish NNUE plumbing (see COPYING).
*/

#include "passed_pawns.h"

#ifdef TRIUMV_VERIFY_V1PASS
    #include <cstdio>
    #include <cstdlib>
#endif

#include "feat_perm.h"

#include <array>

#include "../../bitboard.h"
#include "../../nn_board.h"

namespace Triumviratus::Eval::NNUE::Features {

// Un pedone del colore c in s e' passato se nessun pedone avversario sta nelle case strettamente AVANTI a s sulla sua
// colonna o su quelle adiacenti, e nessun pedone proprio sta strettamente avanti sulla sua colonna (un doppiato
// arretrato non e' un passato). Square RAW: l'orientazione entra solo nell'indice, come la band del PawnPair.
//
// 05/10/2026 — SENZA CICLO. Il ciclo per pedone (0-8 giri, uscita mal predetta; a ogni evento di pedone si chiama
// otto volte per aggiornamento: due colori, prima e dopo, due prospettive) e le due tabelle passedSpan/forwardFile
// diventano un riempimento di bitboard: l'"ombra" degli insiemi qui sopra (le case strettamente DIETRO ogni loro
// pedone, viste dal colore c) con shift a cascata; il pedone e' passato se non cade in nessuna delle due ombre.
// Stesso insieme, stesso albero. xperf 6 giri, build PGO avx512: cicli per nodo -0,81%, salti mal predetti -2,7%.
Bitboard PassedPawns::passers(Color c, Bitboard ownPawns, Bitboard oppPawns) {
    // Verso "indietro" per il colore c. Nella nostra numerazione (a8 = 0, 07/10/2026) il bianco avanza verso gli indici
    // piu' bassi, quindi la sua ombra sale di indice; la colonna e lo spostamento di colonna (<< 1, >> 1) non cambiano.
    const auto back = [c](Bitboard b, int n) { return c == WHITE ? b << n : b >> n; };
    // Pedoni avversari allargati alle colonne adiacenti, poi tutte le case strettamente dietro di loro.
    Bitboard oppShadow = back(oppPawns | ((oppPawns & ~FileHBB) << 1) | ((oppPawns & ~FileABB) >> 1), 8);
    oppShadow |= back(oppShadow, 8);
    oppShadow |= back(oppShadow, 16);
    oppShadow |= back(oppShadow, 32);
    // Case strettamente dietro un pedone proprio sulla stessa colonna.
    Bitboard ownShadow = back(ownPawns, 8);
    ownShadow |= back(ownShadow, 8);
    ownShadow |= back(ownShadow, 16);
    ownShadow |= back(ownShadow, 32);
    return ownPawns & ~oppShadow & ~ownShadow;
}

// Full refresh: one feature per passed pawn of either color.
void PassedPawns::append_active_indices(Color perspective, const NnBoard& pos, IndexList& active) {
    const int      ksq = pos.king(perspective);
    const Bitboard wp  = pos.pawns(WHITE);
    const Bitboard bp  = pos.pawns(BLACK);

    Bitboard pw = passers(WHITE, wp, bp);
    while (pw)
        active.push_back(feat_row(FoldOffset + make_index(perspective, ksq, WHITE, pop_lsb(pw))));

    Bitboard pb = passers(BLACK, bp, wp);
    while (pb)
        active.push_back(feat_row(FoldOffset + make_index(perspective, ksq, BLACK, pop_lsb(pb))));
}

// Incremental: un evento-pedone puo' cambiare lo status passed anche di ALTRI
// pedoni (quelli avversari nella span delle case toccate). Invece di inseguire
// i candidati, ricomputiamo il SET completo dei passati prima/dopo dallo
// snapshot DirtyPawns (<=32 test su maschere, solo su eventi-pedone) e
// XOR-iamo: emissione esatta per costruzione, tutti i casi (cattura,
// promozione, en passant) gestiti uniformemente.
void PassedPawns::append_changed_indices(Color           perspective,
                                         int             ksq,
                                         const DiffType& diff,
                                         IndexList&      removed,
                                         IndexList&      added) {
    if (!diff.any)
        return;

    Bitboard before[COLOR_NB] = {diff.before[WHITE], diff.before[BLACK]};
    Bitboard after[COLOR_NB]  = {before[WHITE], before[BLACK]};
    for (int i = 0; i < diff.nRemoved; i++)
        after[diff.removedC[i]] &= ~(1ULL << diff.removedSq[i]);
    if (diff.addedSq != NN_SQ_NONE)
        after[diff.addedC] |= 1ULL << diff.addedSq;

    for (Color c : {WHITE, BLACK})
    {
        const Bitboard pb  = passers(c, before[c], before[~c]);
        const Bitboard pa  = passers(c, after[c], after[~c]);
        Bitboard       rem = pb & ~pa;
        Bitboard       add = pa & ~pb;
        while (rem)
            removed.push_back(feat_row(FoldOffset + make_index(perspective, ksq, c, pop_lsb(rem))));
        while (add)
            added.push_back(feat_row(FoldOffset + make_index(perspective, ksq, c, pop_lsb(add))));
    }
}

#ifdef TRIUMV_VERIFY_V1PASS
// R4: i passati del recupero (NnStack::v1PassW/B) devono essere quelli della snapshot dei pedoni, prima e dopo.
void PassedPawns::verify_pass(const DiffType& diff, const Bitboard* pb, const Bitboard* pa) {
    Bitboard before[COLOR_NB] = {diff.before[WHITE], diff.before[BLACK]};
    Bitboard after[COLOR_NB]  = {before[WHITE], before[BLACK]};
    for (int i = 0; i < diff.nRemoved; i++)
        after[diff.removedC[i]] &= ~(1ULL << diff.removedSq[i]);
    if (diff.addedSq != NN_SQ_NONE)
        after[diff.addedC] |= 1ULL << diff.addedSq;
    for (Color c : {WHITE, BLACK})
        if (pb[c] != passers(c, before[c], before[~c]) || pa[c] != passers(c, after[c], after[~c]))
        {
            std::fprintf(stderr, "[V1PASS] passati del recupero diversi dalla snapshot (colore %d)\n", int(c));
            std::fflush(stderr);
            std::abort();
        }
}
#endif

}  // namespace Triumviratus::Eval::NNUE::Features
