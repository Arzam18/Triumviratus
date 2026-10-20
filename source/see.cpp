/*
 * Static Exchange Evaluation (SEE)
 * Determines if a capture wins or loses material
 */

#include "see.h"
#include "defs.h"
#include "movegen.h"
#include "magic.h"
#include "attacks.h"
#include <algorithm>
#include <cstdio>    // TRIUMV_VERIFY_SEE2
#include <cstdlib>

// Get least valuable attacker
static inline int get_lva(U64 bb[12], U64 occ[3], int square, int side, int* from_sq) {
    U64 attackers;
    
    // Pawns first (least valuable)
    if (side == white) {
        attackers = pawn_attacks[black][square] & bb[P];
    } else {
        attackers = pawn_attacks[white][square] & bb[p];
    }
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return (side == white) ? P : p;
    }
    
    // Knights
    int knight = (side == white) ? N : n;
    attackers = knight_attacks[square] & bb[knight];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return knight;
    }
    
    // Bishops
    int bishop = (side == white) ? B : b;
    attackers = get_bishop_attacks(square, occ[both]) & bb[bishop];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return bishop;
    }
    
    // Rooks
    int rook = (side == white) ? R : r;
    attackers = get_rook_attacks(square, occ[both]) & bb[rook];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return rook;
    }
    
    // Queens
    int queen = (side == white) ? Q : q;
    attackers = get_queen_attacks(square, occ[both]) & bb[queen];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return queen;
    }
    
    // King (last resort)
    int king = (side == white) ? K : k;
    attackers = king_attacks[square] & bb[king];
    if (attackers) {
        *from_sq = get_ls1b_index(attackers);
        return king;
    }
    
    return -1;
}

// SeeFix (UCI "SeeFix", default ON) — ablazione del FIX P0.2: quando OFF, le mosse
// quiet tornano a SEE=0 (come 3.7: quiet-SEE-pruning e filtro CheckOrdering inerti)
// e l'en-passant torna a rimuovere il pedone dalla casa sbagliata. Definita in threads.cpp.
#ifdef TRIUMV_RELEASE
// Release (06/10/2026): costanti (default di 01_params.inc; SeePinned verificata con static_assert).
constexpr bool g_see_fix = true;
constexpr int  g_see_pinned = 1;
#else
extern bool g_see_fix;
extern int  g_see_pinned;      // SeePinned (copia del parametro della ricerca, 01_params.inc)
#endif

// SEE implementation
int td_see(ThreadData& td, int move) {
    int from = get_move_source(move);
    int to = get_move_target(move);
    int piece = get_move_piece(move);
    int captured = -1;
    
    // Find captured piece
    int start = (td.side == white) ? p : P;
    int end = (td.side == white) ? k : K;
    
    for (int pc = start; pc <= end; pc++) {
        if (get_bit(td.bitboards[pc], to)) {
            captured = pc;
            break;
        }
    }
    
    // En passant: il pedone catturato NON sta su `to` ma su to±8.
    int captured_sq = to;
    if (get_move_enpassant(move)) {
        captured = (td.side == white) ? p : P;
        if (g_see_fix)
            captured_sq = (td.side == white) ? to + 8 : to - 8;   // FIX P0.2b: era rimosso da `to`
    }

    // Ablazione P0.2 (SeeFix off): comportamento 3.7 = SEE 0 per le quiet.
    if (captured == -1 && !g_see_fix) return 0;

    // FIX P0.2 (2026-06-09): SEE anche per le mosse QUIET (prima: return 0).
    // Per una quiet lo scambio inizia con la cattura avversaria meno cara su `to`
    // (gain[0] = 0). Con il vecchio "return 0": (a) il SEE-pruning dei quiet
    // (threads.cpp, SEEQuietMargin) non scattava MAI; (b) il filtro SEE>=-75 del
    // CheckOrdering era un no-op. NB: le promozioni restano fuori scope (il
    // chiamante le esclude gia' dal SEE-pruning).

    // Local copy for simulation
    U64 bb[12], occ[3];
    memcpy(bb, td.bitboards, sizeof(td.bitboards));
    memcpy(occ, td.occupancies, sizeof(td.occupancies));

    int gain[32];
    int depth = 0;
    int current_side = td.side;

    // Initial gain (0 per le quiet: nessun pezzo incassato dalla mossa stessa)
    gain[0] = (captured != -1) ? see_piece_values[captured] : 0;

    // Remove attacker from source
    pop_bit(bb[piece], from);
    pop_bit(occ[current_side], from);
    pop_bit(occ[both], from);

    // Remove captured from its square (== `to` salvo en passant)
    if (captured != -1) {
        pop_bit(bb[captured], captured_sq);
        pop_bit(occ[current_side ^ 1], captured_sq);
        if (captured_sq != to)                     // e.p.: libera anche occ[both]
            pop_bit(occ[both], captured_sq);
    }

    // Attacker now on target
    set_bit(bb[piece], to);
    set_bit(occ[current_side], to);
    set_bit(occ[both], to);
    
    int attacker_value = see_piece_values[piece];
    current_side ^= 1;
    
    // Simulate exchanges
    while (depth < 31) {
        depth++;
        
        int from_sq;
        int attacker = get_lva(bb, occ, to, current_side, &from_sq);
        
        if (attacker == -1) break;
        
        gain[depth] = attacker_value - gain[depth - 1];
        
        // Stand-pat pruning (swap-SEE classico: max(-gain[d-1], gain[d]) < 0).
        // BUG FIX 2026-07-16: la seconda clausola era `gain[d-1] < 0`, insoddisfacibile
        // (se gain[d-1]<0 allora gain[d]=val-gain[d-1]>0) -> break MORTO, prune mai attivo.
        // Corretto = `gain[d-1] > 0`. Value-preserving (stessa SEE, solo esce prima) -> bench invariato.
        if (gain[depth] < 0 && gain[depth - 1] > 0) break;
        
        attacker_value = see_piece_values[attacker];
        
        // Remove this attacker
        pop_bit(bb[attacker], from_sq);
        pop_bit(occ[current_side], from_sq);
        pop_bit(occ[both], from_sq);
        
        // Remove previous piece from target
        for (int pc = P; pc <= k; pc++) {
            if (get_bit(bb[pc], to)) {
                pop_bit(bb[pc], to);
                break;
            }
        }
        
        // Add attacker to target
        set_bit(bb[attacker], to);
        set_bit(occ[current_side], to);
        set_bit(occ[both], to);
        
        current_side ^= 1;
    }
    
    // Negamax the gain stack
    while (--depth > 0) {
        gain[depth - 1] = -((-gain[depth - 1] > gain[depth]) ? -gain[depth - 1] : gain[depth]);
    }

    return gain[0];
}

// ---------------------------------------------------------------------------
// td_see_ge(move, threshold): is the static exchange on the target square worth at least `threshold` for the side
// that moves? Every caller in the engine asks only this question, never the exact value.
// 20/10/2026, written in the textbook swap-list form: the captures on `to` alternate between the sides, each with its
// least valuable attacker; gain[d] is the material balance, for the side that makes capture d, if the exchange stops
// right after it; the list is then resolved backwards, each side being free to stop instead of capturing. Rules:
// - pieces are taken off `occupied` only, and x-ray attackers behind them are added by probing the slider lines
//   again (only the lines a piece of that kind can open: a knight or a king opens none);
// - SeePinned (05/10/2026): while a pinner of the other side still stands (and is not the piece just captured on
//   `to`), our pinned pieces do not recapture (pins computed once per position by see_holds, 11_queue.inc);
// - the king captures only if the square is no longer attacked by the other side;
// - a promotion gains (promoted piece - pawn) and the piece then standing on `to` is the promoted one (SeePromo,
//   17/10/2026, review by Bastiii);
// - the least valuable attacker is found by an index (lowest set bit of the mask of the kinds present, W2,
//   08/10/2026), not by a chain of tests per kind.
// Validation: -DTRIUMV_VERIFY_SEE2 compares every call with the previous form (abort at the first difference).
static int see_ge_impl(ThreadData& td, int move, int threshold) {
    const int from = get_move_source(move);
    const int to   = get_move_target(move);
    int piece      = get_move_piece(move);

    // Victim on `to`: for a pseudo-legal move `to` is empty (-1) or holds an enemy piece.
    int captured = td.piece_on[to];
    U64 occupied = td.occupancies[both] ^ (1ULL << from);   // the mover leaves its square
    if (get_move_enpassant(move)) {
        captured = (td.side == white) ? p : P;
        occupied ^= (td.side == white) ? (1ULL << (to + 8)) : (1ULL << (to - 8));
    }
    int promo_gain = 0;
    if (get_move_promoted(move)) {
        const int promoted = get_move_promoted(move);
        promo_gain = see_piece_values[promoted] - see_piece_values[piece];
        piece = promoted;
    }

    int gain[40];
    int d = 0;
    gain[0] = ((captured != -1) ? see_piece_values[captured] : 0) + promo_gain;
    int standing = see_piece_values[piece];   // value of the piece now on `to`, the next victim

    const U64 diag_sliders = td.bitboards[B] | td.bitboards[b] | td.bitboards[Q] | td.bitboards[q];
    const U64 orth_sliders = td.bitboards[R] | td.bitboards[r] | td.bitboards[Q] | td.bitboards[q];
    const SliderPair sp0 = get_slider_pair(to, occupied);
    U64 attackers = (pawn_attacks[black][to] & td.bitboards[P])
                  | (pawn_attacks[white][to] & td.bitboards[p])
                  | (knight_attacks[to] & (td.bitboards[N] | td.bitboards[n]))
                  | (king_attacks[to] & (td.bitboards[K] | td.bitboards[k]))
                  | (pair_bishop(sp0) & diag_sliders)
                  | (pair_rook(sp0) & orth_sliders);

    // lines a removed piece can open toward `to`: diagonals after a pawn, bishop or queen; files and ranks after a
    // rook or queen (kinds 0..4 = pawn, knight, bishop, rook, queen)
    static constexpr U64 opens_diag[5] = {~0ULL, 0, ~0ULL, 0, ~0ULL};
    static constexpr U64 opens_orth[5] = {0, 0, 0, ~0ULL, ~0ULL};

    for (int side = td.side ^ 1;; side ^= 1) {
        attackers &= occupied;
        U64 mine = attackers & td.occupancies[side];
        if (g_see_pinned && (td.see_pinners[side ^ 1] & occupied & ~(1ULL << to)))
            mine &= ~td.see_pinned[side];
        if (!mine)
            break;
        const int base = (side == white) ? P : p;
        const unsigned kinds = unsigned((mine & td.bitboards[base + 0]) != 0)
                             | unsigned((mine & td.bitboards[base + 1]) != 0) << 1
                             | unsigned((mine & td.bitboards[base + 2]) != 0) << 2
                             | unsigned((mine & td.bitboards[base + 3]) != 0) << 3
                             | unsigned((mine & td.bitboards[base + 4]) != 0) << 4
                             | 32u;
        const int t = __builtin_ctz(kinds);
        if (t == 5) {   // only the king is left: it may capture only an undefended piece, and the exchange ends
            if (!(attackers & td.occupancies[side ^ 1])) {
                gain[d + 1] = standing - gain[d];
                d++;
            }
            break;
        }
        gain[d + 1] = standing - gain[d];
        d++;
        standing = see_piece_values[t];
        const U64 bb = mine & td.bitboards[base + t];
        occupied ^= bb & (0 - bb);
        const SliderPair sp = get_slider_pair(to, occupied);
        attackers |= (pair_bishop(sp) & diag_sliders & opens_diag[t]) | (pair_rook(sp) & orth_sliders & opens_orth[t]);
    }
    // Backwards: the side making capture d takes it only if it does better than stopping before it.
    while (d > 0) {
        gain[d - 1] = std::min(gain[d - 1], -gain[d]);
        d--;
    }
    return gain[0] >= threshold;
}

#ifdef TRIUMV_VERIFY_SEE2
// The previous form (index variant, 08/10/2026), kept only to validate the swap-list form call by call.
template <bool Chain>
static int see_ge_prev(ThreadData& td, int move, int threshold) {
    int from  = get_move_source(move);
    int to    = get_move_target(move);
    int piece = get_move_piece(move);

    // Vittima su 'to': una lettura della scacchiera a caselle (04/10/2026) invece dello scan delle sei bitboard
    // avversarie con un salto ciascuna. Per una mossa pseudo-legale 'to' e' vuota (-1) o ha un pezzo avversario.
    int captured = td.piece_on[to];

    U64 occupied = td.occupancies[both] ^ (1ULL << from);   // il mover lascia l'origine
    if (get_move_enpassant(move)) {
        captured = (td.side == white) ? p : P;
        occupied ^= (td.side == white) ? (1ULL << (to + 8)) : (1ULL << (to - 8));
    }

    // Promotion gain (17/10/2026, review by Bastiii; SPRT on endgames, option SeePromo baked and retired): a promotion
    // gains (promoted piece - pawn) and the piece the opponent may recapture on 'to' is the promoted one. Before, the
    // search never sent promotions here (SEE = 0) and the move counted as a pawn move.
    int promo_gain = 0;
    if (get_move_promoted(move)) {
        const int promoted = get_move_promoted(move);
        promo_gain = see_piece_values[promoted] - see_piece_values[piece];
        piece = promoted;
    }

    // Bilancio se lo scambio si ferma subito dopo la nostra mossa.
    int swap = ((captured != -1) ? see_piece_values[captured] : 0) + promo_gain - threshold;
    if (swap < 0) return 0;      // nemmeno incassando la vittima si arriva alla soglia
    // L'avversario ricattura il nostro pezzo: se restiamo comunque sopra, deciso.
    swap = see_piece_values[piece] - swap;
    if (swap <= 0) return 1;

    // Attaccanti di 'to' di ENTRAMBI i lati. I bb[] restano interi (nessuna copia):
    // si maschera con `occupied` a ogni giro, i pezzi "pelati" spariscono da soli.
    const U64 diag_sliders = td.bitboards[B] | td.bitboards[b] | td.bitboards[Q] | td.bitboards[q];
    const U64 orth_sliders = td.bitboards[R] | td.bitboards[r] | td.bitboards[Q] | td.bitboards[q];
    // AA1 (10/10/2026): alfiere e torre da un solo calcolo (magic.h, get_slider_pair), qui e negli x-ray di W2.
    const SliderPair sp0 = get_slider_pair(to, occupied);
    U64 attackers =
          (pawn_attacks[black][to] & td.bitboards[P])
        | (pawn_attacks[white][to] & td.bitboards[p])
        | (knight_attacks[to] & (td.bitboards[N] | td.bitboards[n]))
        | (king_attacks[to]   & (td.bitboards[K] | td.bitboards[k]))
        | (pair_bishop(sp0) & diag_sliders)
        | (pair_rook(sp0)   & orth_sliders);

    int stm = td.side;
    int res = 1;                 // 1 = "la soglia regge" per il lato che ha mosso

    while (true) {
        stm ^= 1;
        attackers &= occupied;
        U64 stm_att = attackers & td.occupancies[stm];   // attackers e' gia' <= occupied
        if (!stm_att) break;                             // niente attaccanti: res corrente decide
        // SeePinned (05/10/2026, audit del nucleo; regola di SF): finche' un inchiodatore avversario e' ancora sulla
        // scacchiera (e non e' il pezzo appena catturato su `to`), i nostri pezzi inchiodati non ricatturano.
        // Inchiodati e inchiodatori li calcola see_holds (11_queue.inc) una volta per posizione.
        if (g_see_pinned && (td.see_pinners[stm ^ 1] & occupied & ~(1ULL << to))) {
            stm_att &= ~td.see_pinned[stm];
            if (!stm_att) break;
        }
        res ^= 1;

        const int base = (stm == white) ? P : p;
        if constexpr (!Chain) {
            // diagonali dopo pedone, alfiere, donna (tipi 0, 2, 4); ortogonali dopo torre e donna (3, 4)
            static constexpr U64 diag_mask[5] = {~0ULL, 0, ~0ULL, 0, ~0ULL};
            static constexpr U64 orth_mask[5] = {0, 0, 0, ~0ULL, ~0ULL};
            const unsigned kinds = unsigned((stm_att & td.bitboards[base + 0]) != 0)
                                 | unsigned((stm_att & td.bitboards[base + 1]) != 0) << 1
                                 | unsigned((stm_att & td.bitboards[base + 2]) != 0) << 2
                                 | unsigned((stm_att & td.bitboards[base + 3]) != 0) << 3
                                 | unsigned((stm_att & td.bitboards[base + 4]) != 0) << 4
                                 | 32u;
            const int t = __builtin_ctz(kinds);
            if (t == 5)                                         // re: cattura legale solo se INDIFESO
                return ((attackers & occupied) & td.occupancies[stm ^ 1]) ? (res ^ 1) : res;
            if ((swap = see_piece_values[t] - swap) < res) break;
            const U64 bb = stm_att & td.bitboards[base + t];
            occupied ^= (bb & (0 - bb));
            // Skipping the slider lookup when a knight recaptures (17/10/2026, review by Bastiii: a knight opens no
            // line) was measured with xperf on the Xeon: +0.2% instructions, cycles within noise. Not adopted: the
            // loop stays branch-free (W2).
            {
                const SliderPair sp = get_slider_pair(to, occupied);
                attackers |= (pair_bishop(sp) & diag_sliders & diag_mask[t])
                           | (pair_rook(sp)   & orth_sliders & orth_mask[t]);
            }
            continue;
        }

        U64 bb;
        if ((bb = stm_att & td.bitboards[base + 0])) {          // pedone
            if ((swap = see_piece_values[P] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
            attackers |= get_bishop_attacks(to, occupied) & diag_sliders;  // x-ray diagonale
        }
        else if ((bb = stm_att & td.bitboards[base + 1])) {     // cavallo
            if ((swap = see_piece_values[N] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
        }
        else if ((bb = stm_att & td.bitboards[base + 2])) {     // alfiere
            if ((swap = see_piece_values[B] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
            attackers |= get_bishop_attacks(to, occupied) & diag_sliders;
        }
        else if ((bb = stm_att & td.bitboards[base + 3])) {     // torre
            if ((swap = see_piece_values[R] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
            attackers |= get_rook_attacks(to, occupied) & orth_sliders;    // x-ray ortogonale
        }
        else if ((bb = stm_att & td.bitboards[base + 4])) {     // donna
            if ((swap = see_piece_values[Q] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
            attackers |= (get_bishop_attacks(to, occupied) & diag_sliders)
                       | (get_rook_attacks(to, occupied)   & orth_sliders);
        }
        else {                                                  // re: cattura legale solo se INDIFESO
            return ((attackers & occupied) & td.occupancies[stm ^ 1]) ? (res ^ 1) : res;
        }
    }
    return res;
}
#endif

int td_see_ge(ThreadData& td, int move, int threshold) {
#ifdef TRIUMV_VERIFY_SEE2
    const int a = see_ge_impl(td, move, threshold), o = see_ge_prev<false>(td, move, threshold);
    if (a != o) {
        printf("info string SEE2 MISMATCH: move %d threshold %d new %d previous %d\n", move, threshold, a, o);
        fflush(stdout);
        abort();
    }
    return a;
#else
    return see_ge_impl(td, move, threshold);
#endif
}
