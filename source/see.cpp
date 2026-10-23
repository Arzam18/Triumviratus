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
#ifdef TRIUMV_C2
#include "nn_dirty.h"            // C2 piece values (agent SEE): the expert pair on the network's stack
#include "consilium_bridge.h"    // nn_router_table, cn_sig (the pair at the root)
#endif

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

// SeeFix (UCI "SeeFix", default ON) - ablation of FIX P0.2: when OFF, the quiet
// moves go back to SEE=0 (as in 3.7: quiet SEE pruning and the CheckOrdering filter inert)
// and en passant goes back to removing the pawn from the wrong square. Defined in threads.cpp.
#ifdef TRIUMV_RELEASE
// Release (06/10/2026): constants (defaults of 01_params.inc; SeePinned checked with static_assert).
constexpr bool g_see_fix = true;
constexpr int  g_see_pinned = 1;
static constexpr const int* g_see_val = see_piece_values;   // SeeVal* (agent SEE, 21/10/2026; static_assert there)
#else
extern bool g_see_fix;
extern int  g_see_pinned;      // SeePinned (copy of the search parameter, 01_params.inc)
#endif

// --- C2 piece values (agent SEE) ---
// REGIME PIECE VALUES (21/10/2026, idea 1 of docs/rete/C2_IDEE_FABLE_2026-10-21.md; OFF by default; SPRT/SPSA pending).
// Consilium Plebis routes each perspective to an expert by the OPPONENT's non-pawn material (e0 at most a minor, e1 a lone
// rook, e2 two minors or rook + minor, e3 a lone queen, e4 10-13, e5 14-18, e6 19-25, e7 26-31 points). The expert of
// the Black perspective therefore reads White's material, and the reverse. With C2SeeReg on, a piece is valued by the
// regime of its OWNER's material: White's pieces take the offsets of the Black perspective's expert, Black's pieces
// those of the White perspective's expert (the proposal's choice; the other side's reading is the "own rows" half of
// the network's PSQT, tools/c2_psqt_seeds.py). value(piece) = SeeVal<type> + C2Val<type><expert>, pawns and kings
// without offset (the pawn is the unit of the scale).
// The two tables (values and offsets, 16 piece codes each) live in the thread and are rebuilt only when the expert
// pair changes: the pair is one byte on the network's stack (dp.phase, written by the make), so the check per call is
// one load and one compare; at the root of a search (one state) the pair is read from the board. copy_board_to_thread
// marks the table stale (c2_see_pair = -1) at each search start, which also catches a setoption in between.
// C2SeeReg bits: 1 = the exchange evaluation (this file); 2 = the victim of the quiescence futility (qs_worth,
// 12_quiesce.inc); 4 = the victim of the capture futility and of the capture order (capture_gate, score_tactic).
// Least valuable attacker: the swap list keeps taking the attackers by kind (pawn, knight, bishop, rook, queen) even
// where the offsets would reorder two kinds; the values only change the balance of the exchange.
#ifdef TRIUMV_C2
#ifdef TRIUMV_RELEASE
constexpr int g_c2_see_reg = 0;                   // C2SeeReg (static_assert in 01_params.inc)
static constexpr int g_c2_val_off[4][8] = {};      // C2ValN..C2ValQ, experts 0..7
#else
extern int g_c2_see_reg;                           // copies of C2SeeReg and C2Val*, synced at setoption
extern int g_c2_val_off[4][8];
#endif

// Expert pair of the thread's position, White | Black << 4 (the same byte as c2_experts_td, 06_nndirty.inc).
static inline int c2_pair_now(const ThreadData& td) {
    const NnStack& S = *td.nnstack;
    if (S.size >= 2)
        return S.st[S.size - 1].dp.phase;
    int sig[2];
    for (int c = 0; c < 2; c++)
        sig[c] = cn_sig(count_bits(td.bitboards[N + 6 * c]), count_bits(td.bitboards[B + 6 * c]),
                        count_bits(td.bitboards[R + 6 * c]), count_bits(td.bitboards[Q + 6 * c]));
    const unsigned char* rt = nn_router_table();
    return rt[sig[0] * 54 + sig[1]] | rt[sig[1] * 54 + sig[0]] << 4;
}

static void c2_see_rebuild(const ThreadData& td, int pair) {
    const int e_of_owner[2] = {(pair >> 4) & 7, pair & 7};   // White's pieces: Black's expert; Black's: White's
    for (int c = 0; c < 2; c++)
        for (int t = 0; t < 6; t++) {
            const int off = (t >= N && t <= Q) ? g_c2_val_off[t - N][e_of_owner[c]] : 0;
            td.c2_see_off[6 * c + t] = off;
            td.c2_see_val[6 * c + t] = g_see_val[6 * c + t] + off;
        }
    for (int i = 12; i < 16; i++)
        td.c2_see_off[i] = td.c2_see_val[i] = 0;
    td.c2_see_pair = pair;
}

static inline void c2_see_sync(const ThreadData& td) {
    const int pair = c2_pair_now(td);
    if (pair != td.c2_see_pair)
        c2_see_rebuild(td, pair);
}

const int* see_c2_offsets(const ThreadData& td) {
    c2_see_sync(td);
    return td.c2_see_off;
}

// Values the SEE reads: the regime table when bit 1 of C2SeeReg is on, otherwise SeeVal*.
static inline const int* see_values(const ThreadData& td) {
    if (g_c2_see_reg & 1) {
        c2_see_sync(td);
        return td.c2_see_val;
    }
    return g_see_val;
}
#else
static inline const int* see_values(const ThreadData&) { return g_see_val; }
#endif

// SEE implementation
int td_see(ThreadData& td, int move) {
    const int* const val = see_values(td);   // piece values (SeeVal*, or the C2 regime table)
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
    
    // En passant: the captured pawn is NOT on `to` but on to±8.
    int captured_sq = to;
    if (get_move_enpassant(move)) {
        captured = (td.side == white) ? p : P;
        if (g_see_fix)
            captured_sq = (td.side == white) ? to + 8 : to - 8;   // FIX P0.2b: it was removed from `to`
    }

    // Ablation P0.2 (SeeFix off): 3.7 behaviour = SEE 0 for quiet moves.
    if (captured == -1 && !g_see_fix) return 0;

    // FIX P0.2 (2026-06-09): SEE for QUIET moves too (before: return 0).
    // For a quiet move the exchange starts with the opponent's cheapest capture on `to`
    // (gain[0] = 0). With the old "return 0": (a) the quiet SEE pruning
    // (threads.cpp, SEEQuietMargin) NEVER fired; (b) the SEE>=-75 filter of
    // CheckOrdering was a no-op. NB: promotions stay out of scope (the
    // caller already excludes them from the SEE pruning).

    // Local copy for simulation
    U64 bb[12], occ[3];
    memcpy(bb, td.bitboards, sizeof(td.bitboards));
    memcpy(occ, td.occupancies, sizeof(td.occupancies));

    int gain[32];
    int depth = 0;
    int current_side = td.side;

    // Initial gain (0 for quiet moves: no piece collected by the move itself)
    gain[0] = (captured != -1) ? val[captured] : 0;

    // Remove attacker from source
    pop_bit(bb[piece], from);
    pop_bit(occ[current_side], from);
    pop_bit(occ[both], from);

    // Remove captured from its square (== `to` salvo en passant)
    if (captured != -1) {
        pop_bit(bb[captured], captured_sq);
        pop_bit(occ[current_side ^ 1], captured_sq);
        if (captured_sq != to)                     // e.p.: frees occ[both] too
            pop_bit(occ[both], captured_sq);
    }

    // Attacker now on target
    set_bit(bb[piece], to);
    set_bit(occ[current_side], to);
    set_bit(occ[both], to);
    
    int attacker_value = val[piece];
    current_side ^= 1;
    
    // Simulate exchanges
    while (depth < 31) {
        depth++;
        
        int from_sq;
        int attacker = get_lva(bb, occ, to, current_side, &from_sq);
        
        if (attacker == -1) break;
        
        gain[depth] = attacker_value - gain[depth - 1];
        
        // Stand-pat pruning (classic swap SEE: max(-gain[d-1], gain[d]) < 0).
        // BUG FIX 2026-07-16: the second clause was `gain[d-1] < 0`, unsatisfiable
        // (if gain[d-1]<0 then gain[d]=val-gain[d-1]>0) -> DEAD break, prune never active.
        // Correct = `gain[d-1] > 0`. Value-preserving (same SEE, it only exits earlier) -> bench unchanged.
        if (gain[depth] < 0 && gain[depth - 1] > 0) break;
        
        attacker_value = val[attacker];
        
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
// The two bounds of the resolved list are tested first (the walk is needed only between them).
// Validation: -DTRIUMV_VERIFY_SEE2 compares every call with the previous form (abort at the first difference).
static int see_ge_impl(ThreadData& td, int move, int threshold) {
    const int from = get_move_source(move);
    const int to   = get_move_target(move);
    int piece      = get_move_piece(move);
    // Piece values by piece code (agent SEE, 21/10/2026): SeeVal*, or with C2SeeReg the regime table of the thread,
    // where the two colours can differ (a kind index t is turned into a code with the side's base below).
    const int* const val = see_values(td);

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
        promo_gain = val[promoted] - val[piece];
        piece = promoted;
    }

    int gain[40];
    int d = 0;
    gain[0] = ((captured != -1) ? val[captured] : 0) + promo_gain;
    int standing = val[piece];   // value of the piece now on `to`, the next victim

    // Bounds of the resolved list, known before any attacker is looked up (20/10/2026, speed, same answers): the
    // backward pass only lowers gain[0], so the result is at most gain[0]; and it gives gain[0] >= min(gain[0],
    // -gain[1]) with gain[1] at most standing - gain[0], so the result is at least gain[0] - standing (after any
    // recapture we can stop). Most calls are decided here.
    if (gain[0] < threshold)
        return 0;
    if (gain[0] - standing >= threshold)
        return 1;

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
#if defined(TRIUMV_C2) && !defined(TRIUMV_RELEASE)
        standing = val[base + t];   // the regime table may differ between the colours
#else
        standing = val[t];          // both colours share the values (same code as before)
#endif
        const U64 bb = mine & td.bitboards[base + t];
        occupied ^= bb & (0 - bb);
        const SliderPair sp = get_slider_pair(to, occupied);
        attackers |= (pair_bishop(sp) & diag_sliders & opens_diag[t]) | (pair_rook(sp) & orth_sliders & opens_orth[t]);
    }
    // Backwards: the side making capture d takes it only if it does better than stopping before it.
    while (d > 0) {
        gain[d - 1] = (std::min)(gain[d - 1], -gain[d]);   // parentheses: windows.h min macro under clang-cl
        d--;
    }
    return gain[0] >= threshold;
}

#ifdef TRIUMV_VERIFY_SEE2
// The previous form (index variant, 08/10/2026), kept only to validate the swap-list form call by call.
template <bool Chain>
static int see_ge_prev(ThreadData& td, int move, int threshold) {
    const int* const val = see_values(td);   // same values as see_ge_impl (agent SEE, 21/10/2026), by piece code
    int from  = get_move_source(move);
    int to    = get_move_target(move);
    int piece = get_move_piece(move);

    // Victim on 'to': one read of the mailbox board (04/10/2026) instead of scanning the six enemy bitboards
    // with a branch each. For a pseudo-legal move 'to' is empty (-1) or holds an enemy piece.
    int captured = td.piece_on[to];

    U64 occupied = td.occupancies[both] ^ (1ULL << from);   // the mover leaves the origin
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
        promo_gain = val[promoted] - val[piece];
        piece = promoted;
    }

    // Balance if the exchange stops right after our move.
    int swap = ((captured != -1) ? val[captured] : 0) + promo_gain - threshold;
    if (swap < 0) return 0;      // not even collecting the victim reaches the threshold
    // The opponent recaptures our piece: if we stay above anyway, decided.
    swap = val[piece] - swap;
    if (swap <= 0) return 1;

    // Attackers of 'to' of BOTH sides. The bb[] stay whole (no copy):
    // they are masked with `occupied` at every round, the "peeled" pieces disappear by themselves.
    const U64 diag_sliders = td.bitboards[B] | td.bitboards[b] | td.bitboards[Q] | td.bitboards[q];
    const U64 orth_sliders = td.bitboards[R] | td.bitboards[r] | td.bitboards[Q] | td.bitboards[q];
    // AA1 (10/10/2026): bishop and rook from a single computation (magic.h, get_slider_pair), here and in the x-rays of W2.
    const SliderPair sp0 = get_slider_pair(to, occupied);
    U64 attackers =
          (pawn_attacks[black][to] & td.bitboards[P])
        | (pawn_attacks[white][to] & td.bitboards[p])
        | (knight_attacks[to] & (td.bitboards[N] | td.bitboards[n]))
        | (king_attacks[to]   & (td.bitboards[K] | td.bitboards[k]))
        | (pair_bishop(sp0) & diag_sliders)
        | (pair_rook(sp0)   & orth_sliders);

    int stm = td.side;
    int res = 1;                 // 1 = "the threshold holds" for the side that moved

    while (true) {
        stm ^= 1;
        attackers &= occupied;
        U64 stm_att = attackers & td.occupancies[stm];   // attackers is already <= occupied
        if (!stm_att) break;                             // no attackers: the current res decides
        // SeePinned (05/10/2026, core audit; SF's rule): while an enemy pinner is still on the
        // board (and is not the piece just captured on `to`), our pinned pieces do not recapture.
        // Pinned pieces and pinners are computed by see_holds (11_queue.inc) once per position.
        if (g_see_pinned && (td.see_pinners[stm ^ 1] & occupied & ~(1ULL << to))) {
            stm_att &= ~td.see_pinned[stm];
            if (!stm_att) break;
        }
        res ^= 1;

        const int base = (stm == white) ? P : p;
        if constexpr (!Chain) {
            // diagonals after pawn, bishop, queen (types 0, 2, 4); orthogonals after rook and queen (3, 4)
            static constexpr U64 diag_mask[5] = {~0ULL, 0, ~0ULL, 0, ~0ULL};
            static constexpr U64 orth_mask[5] = {0, 0, 0, ~0ULL, ~0ULL};
            const unsigned kinds = unsigned((stm_att & td.bitboards[base + 0]) != 0)
                                 | unsigned((stm_att & td.bitboards[base + 1]) != 0) << 1
                                 | unsigned((stm_att & td.bitboards[base + 2]) != 0) << 2
                                 | unsigned((stm_att & td.bitboards[base + 3]) != 0) << 3
                                 | unsigned((stm_att & td.bitboards[base + 4]) != 0) << 4
                                 | 32u;
            const int t = __builtin_ctz(kinds);
            if (t == 5)                                         // king: capture legal only if UNDEFENDED
                return ((attackers & occupied) & td.occupancies[stm ^ 1]) ? (res ^ 1) : res;
            if ((swap = val[base + t] - swap) < res) break;
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
        if ((bb = stm_att & td.bitboards[base + 0])) {          // pawn
            if ((swap = val[base + 0] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
            attackers |= get_bishop_attacks(to, occupied) & diag_sliders;  // x-ray diagonale
        }
        else if ((bb = stm_att & td.bitboards[base + 1])) {     // knight
            if ((swap = val[base + 1] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
        }
        else if ((bb = stm_att & td.bitboards[base + 2])) {     // bishop
            if ((swap = val[base + 2] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
            attackers |= get_bishop_attacks(to, occupied) & diag_sliders;
        }
        else if ((bb = stm_att & td.bitboards[base + 3])) {     // rook
            if ((swap = val[base + 3] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
            attackers |= get_rook_attacks(to, occupied) & orth_sliders;    // x-ray ortogonale
        }
        else if ((bb = stm_att & td.bitboards[base + 4])) {     // queen
            if ((swap = val[base + 4] - swap) < res) break;
            occupied ^= (bb & (0 - bb));
            attackers |= (get_bishop_attacks(to, occupied) & diag_sliders)
                       | (get_rook_attacks(to, occupied)   & orth_sliders);
        }
        else {                                                  // king: capture legal only if UNDEFENDED
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
