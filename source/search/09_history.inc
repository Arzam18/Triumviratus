// search/09_history.inc -- Static evaluation, move statistics, correction of the evaluation.
// Part of threads.cpp: NOT a translation unit of its own.

// ============================================================================
// EVALUATION
// ============================================================================

// "eval" command: static evaluation of the global board (side to move) and the raw network output. The network reads
// our bitboards as they are (07/10/2026, single board).
int debug_eval_position() {
  return nn_eval(side == white, bitboards, occupancies, fifty);
}
int debug_eval_position_raw() {
  int raw = 0;
  nn_eval(side == white, bitboards, occupancies, fifty, &raw);
  return raw;
}

// Back to the root with the network's chain of accumulators (at every iteration). The network reads the thread's
// board directly: there is no copy to realign, only the dirty stack to reset.
static inline void nn_root_sync(ThreadData &td) {
  nn_pos_set(td.nnpos);
#ifdef TRIUMV_VERIFY_NNSYNC
  td.nnstack->st[0].key = nn_piece_key(td);
#endif
}

// The network evaluates the thread's board. First the missing threats and pawns (nn_dirty_catch_up) and, with
// -DTRIUMV_VERIFY_NNSYNC, the check that the stack describes this board (06_nndirty.inc).
static inline int td_nn_eval(ThreadData &td) {
  {
    PROF_GUARD(prof_catchup);
    nn_dirty_catch_up(td);
  }
  nn_verify_sync(td);
  return nn_pos_eval(td.nnpos, td.bitboards, td.occupancies, td.piece_on, td.side == white, td.fifty);
}

// The evaluation is damped with the fifty-move counter: v * (199 - fifty) / 199. The hash keeps the value WITHOUT the
// damping, so that it holds for every transposition, and damps it again when read.
// (08/10/2026, speed, same value) The divisor 199 - fifty changes at run time and the division was an idiv (~26
// cycles of latency) at every fresh evaluation, twice. Now it multiplies by the reciprocal M = ceil(2^40 / D) and
// truncates toward zero: exact while |x| * (M*D - 2^40) < 2^40, that is |x| < 2^40 / 199; here |x| = |v| * 199 < 2^28
// (checked for every fifty and |v| <= 2^20: no difference). Identical tree.
static constexpr auto unfade_recip = [] {
  std::array<unsigned long long, 101> r{};
  for (int f = 0; f <= 100; f++)
    r[f] = ((1ULL << 40) + (unsigned long long)(198 - f)) / (unsigned long long)(199 - f);
  return r;
}();
static inline int eval_unfade(int v, int fifty) {
  fifty = std::min(fifty, 100);
  const long long x = (long long)v * 199;
  const unsigned long long mag = (unsigned long long)(x < 0 ? -x : x);
  const int q = (int)((mag * unfade_recip[fifty]) >> 40);
  return x < 0 ? -q : q;
}
static inline int eval_refade(int v, int fifty) {
  if (fifty > 100) fifty = 100;
  // The same truncation as the bridge (nn_scale, nn_finalize: v - v*f/199), so the same position has the same value
  // fresh, from the cache or from the hash (05/10/2026; before, v * (199 - fifty) / 199).
  return v - v * fifty / 199;
}

// Expert of the network (material band) by number of pieces: <= 9, 10-15, 16-23, >= 24, the bands of the network's
// piece-square block. Used by the per-expert parameters and CorrPhase.
static inline int net_phase(int pieces) { return pieces <= 9 ? 0 : pieces <= 15 ? 1 : pieces <= 23 ? 2 : 3; }

// Network evaluation (network units).
static inline int net_evaluate(ThreadData &td) {
  PROF_GUARD(prof_eval);
  if (g_eval_off) {
    static const int mat[6] = {100, 320, 330, 500, 900, 0};
    int s = 0;
    for (int pc = 0; pc < 6; pc++)
      s += mat[pc] * (count_bits(td.bitboards[pc]) - count_bits(td.bitboards[pc + 6]));
    return td.side == white ? s : -s;
  }
  return td_nn_eval(td);   // (15/10/2026) no per-thread evaluation cache, see threads.h
}

// Evaluation in the units of the search margins: x EvalUnits/1024, inside the tablebase band.
static inline int static_evaluate(ThreadData &td) {
  const int v = (int)((long long)net_evaluate(td) * EvalUnits / 1024);
  return std::clamp(v, -TB_WIN_FLOOR + 1, TB_WIN_FLOOR - 1);
}


// Corrections learned with keys on residual concepts (11/10/2026): all closed and removed. CorrKingPawnW (all pawns +
// the 2x2 zones of the kings) -4.68 +- 7.19 over 2,378 games, key too fine; CorrShelterW (pawns on the three files
// around the kings) -5.63 +- 5.53 over 3,898; CorrPassKingW (passed pawns outside the enemy king's square) -5.24 +-
// 8.49 over 744 endgames. The existing corrections seem to take the signal that games can teach already.

// Value for the screen: from search units to centipawns (network scale and normalisation by 400).
// (06/10/2026) 449 -> 400, so that +1.00 = 50% chance of winning, fitted on our own self-play data
// (match_moe/fit_cp_scale.py): with 449 the 50% point fell at +0.89 printed, flat with the material, at 10+0.1
// (563,496 positions) and 40+0.4 (99,438): one constant is enough. Display only: the search uses internal units and
// the bench does not change.
static inline int units_to_cp(int v) {
  const long long net = (long long)v * 1024 / EvalUnits;
  const int es = nn_get_eval_scale();
  const long long cp = es == 100 ? net : net * 100 / es;
  return (int)(cp * 100 / 400);
}

// ============================================================================
// MOVE STATISTICS
// ============================================================================

// Value of a piece type in the units of the margins (index: P..K, then "none").
static const int unit_value[7] = {208, 781, 825, 1276, 2538, 0, 0};

// Every statistic of the engine (move histories, correction histories, hash-move history) is updated by
// hist_learn, below. The former update (stat_push, in the inherited form) was removed on 17/10/2026: hist_learn with
// gravity 1024 gives the same integer result.

// (12/10/2026) The three limits are parameters (HistLim*, 01_params.inc); in the release they are constants.
#ifdef TRIUMV_RELEASE
constexpr int LIM_QUIET  = HistLimQuiet;
constexpr int LIM_CAPT   = HistLimCapt;
constexpr int LIM_FOLLOW = HistLimFollow;
#else
#define LIM_QUIET  HistLimQuiet
#define LIM_CAPT   HistLimCapt
#define LIM_FOLLOW HistLimFollow
#endif
constexpr int LIM_PAWN   = 8192;
constexpr int LIM_CORR   = 1024;
constexpr int LIM_TTMOVE = 8192;

static inline bool move_ok(int m) { return m > 0; }
static inline int  move_from(int m) { return get_move_source(m); }
static inline int  move_to(int m) { return get_move_target(m); }
static inline int  move_piece(int m) { return get_move_piece(m); }
// Promotion kind 0..4 (none, N, B, R, Q): part of the key of the quiet statistics. A table on the 4 bits of the field
// instead of `pr ? pr % 6 : 0` (same values): promo_kind runs for every move and cost 0.6% of the cycles.
static constexpr signed char kPromoKind[16] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3};
static inline int  promo_kind(int m) { return kPromoKind[get_move_promoted(m)]; }
static inline int  quiet_key(int m) { return move_from(m) | (move_to(m) << 6) | (promo_kind(m) << 12); }
// "Tactical" move: a capture or a queen promotion (code Q = 4 or q = 10: bits 4 and 10 of the mask 0x410). The other
// promotions are ordered among the quiet moves.
static inline bool is_tactical(int m) { return (get_move_capture(m) | ((0x410u >> get_move_promoted(m)) & 1)) != 0; }
// Piece type P..K from a code P..k without a modulo (06/10/2026, speed, same values). Index 15 is "no piece"
// (-1 & 15) and gives CAPT_NONE, so an empty square also goes through the table without a branch.
static constexpr signed char kPieceKind[16] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, CAPT_NONE, CAPT_NONE, CAPT_NONE, CAPT_NONE};
// Type of the captured piece (CAPT_NONE for en passant and for a promotion without capture, like an empty square).
static inline int  victim_kind(const ThreadData &td, int m) {
  return kPieceKind[td.piece_on[move_to(m)] & 15];
}

static inline PieceToRow &pawn_row(const ThreadData &td) {
  return g_stats.pawn_rows[td.pawn_key & g_stats.pawn_mask];
}

// ============================================================================
// WRITING THE STATISTICS (block 1 of 8.1)
// Every update of a statistic goes through the functions below, with the parameters of the network expert chosen by
// the material of the node (net_phase: 0 = 9 pieces or fewer, 1 = 10-15, 2 = 16-23, 3 = 24 or more).
// Origin: the saturating update and an amount linear in depth are known ideas of move histories, used by most
// engines. Our form: a gravity per expert, a quadratic term in depth, weights per expert.
// At the starting values every formula gives the old integer result, so the tree is identical until tuning moves
// them (bench 137419). Parameters: 01_params.inc, sub-blocks E, F, G of docs/development/8.1/SEARCH_1_ORDERING.md.
// ============================================================================
static inline int by_exp4(int e, int v0, int v1, int v2, int v3) { return e == 0 ? v0 : e == 1 ? v1 : e == 2 ? v2 : v3; }
static inline int hist_grav(int e)   { return by_exp4(e, HlGrav0, HlGrav1, HlGrav2, HlGrav3); }
static inline int hist_cgrav(int e)  { return by_exp4(e, HlCGrav0, HlCGrav1, HlCGrav2, HlCGrav3); }
static inline int corr_grav(int e)   { return by_exp4(e, CorrGrav0, CorrGrav1, CorrGrav2, CorrGrav3); }
static inline int ttm_grav(int e)    { return by_exp4(e, TtmGrav0, TtmGrav1, TtmGrav2, TtmGrav3); }

// Saturating update: an entry moves toward the reward b and slows near the limit L,
//   e += b - G/1024 * e*|b|/L
// G = 1024 is the old update with the same truncation (e*|b|*1024 / (L*1024) = e*|b| / L). Lower G: the entry gets
// closer to the limit (keeps more); higher G: it forgets sooner. The int16 clamp never acts at G = 1024.
// (20/10/2026) The quotient in double instead of the 64-bit division (same values, checked call by call) was neutral
// in the release build on the Xeon (-0.15% cycles in the middlegame, -0.03% in endgames): not kept.
static inline void hist_learn(int16_t &e, int bonus, int limit, int grav) {
  const int b = std::clamp(bonus, -limit, limit);
  const int v = e;
  const long long pull = (long long)v * (b < 0 ? -b : b) * grav / ((long long)limit * 1024);
  e = (int16_t)std::clamp((long long)v + b - pull, -32767LL, 32767LL);
}
// Same rule for an int statistic (tt_move_hist).
static inline void hist_learn(int &e, int bonus, int limit, int grav) {
  const int b = std::clamp(bonus, -limit, limit);
  const long long pull = (long long)e * (b < 0 ? -b : b) * grav / ((long long)limit * 1024);
  e = (int)((long long)e + b - pull);
}

// Amount of a reward or penalty at depth d: min(mul*d - sub + q*d*d/64, cap), q per expert (HlDepthQ; 0 = the old
// linear form). Every reward and penalty by depth comes from here.
static inline int hist_amount(int e, int depth, int mul, int sub, int cap) {
  const int q = by_exp4(e, HlDepthQ0, HlDepthQ1, HlDepthQ2, HlDepthQ3);
  return std::min(mul * depth - sub + q * depth * depth / 64, cap);
}

// Continuation histories: the move (pc, to) after the moves made 1..6 plies back.
// - weight per distance (ContDist*) and a factor that grows with the number of distances already rewarding the move
//   (ContAgree*): the values are those of Stockfish 19, never tuned by us (they were not among the RW1 levers);
// - per expert: near (1-2) and far (3-6) weights, the fixed reward at distance 1 (ContNear), how many distances are
//   updated when the node is in check (ContChk), the gravity of the quiet histories.
// Defaults (ContNear 73, ContChk 2) give the old tree.
static void learn_followups(NodeFrame *f, int pc, int to, int bonus, int ex) {
  const int dist_weight[7] = {0, ContDist1, ContDist2, ContDist3, ContDist4, ContDist5, ContDist6};
  const int agree_mul[7] = {ContAgree0, ContAgree1, ContAgree2, ContAgree3, ContAgree4, ContAgree5, ContAgree6};
  const int grav = hist_grav(ex);
  const int w_near = by_exp4(ex, HlFNear0, HlFNear1, HlFNear2, HlFNear3);
  const int w_far = by_exp4(ex, HlFFar0, HlFFar1, HlFFar2, HlFFar3);
  const int near_bonus = by_exp4(ex, ContNear0, ContNear1, ContNear2, ContNear3);
  const int last_d = f->in_check ? by_exp4(ex, ContChk0, ContChk1, ContChk2, ContChk3) : 6;
  int agreeing = 0;
  for (int d = 1; d <= last_d; d++) {
    if (!move_ok(f[-d].move))
      continue;
    int16_t &cell = (*f[-d].cont_hist)[pc][to];
    if (cell > 0)
      agreeing++;
    const int wb = bonus * dist_weight[d] * agree_mul[agreeing] / 65536;
    hist_learn(cell, wb * (d <= 2 ? w_near : w_far) / 1024 + (d < 2 ? near_bonus : 0), LIM_FOLLOW, grav);
  }
}

// ============================================================================
// QUIET HISTORY BY THREAT CONTEXT (ThreatHist, block 1 of 8.1)
// The same quiet move means different things when the piece escapes a threat or walks into one. Context, two bits:
// 1 = the from-square is threatened, 2 = the to-square is. Threatened = attacked by an enemy piece of lower value
// (the attack that costs material even on a defended square): for a pawn a pawn, for a knight or bishop a pawn, for a
// rook a minor piece or a pawn, for a queen also a rook. The king and promotions have no context. The maps are the
// ones score_quiets already builds (MoveQueue::menace); a single move (hash move, evasions) asks the squares.
// Origin: indexing the quiet history by threats is a known idea (other engines, e.g. Ethereal). Ours: the
// lower-value context, the factored form and the weights.
//
// Factored form: the flat table (index key) receives every update; three correction tables, one per context, learn
// only how the move does in that context against its flat history. The ordering reads the sum (qh_order); the
// reductions and the history pruning read the flat table only (qh_main), so their parameters keep their meaning.
// ThreatCtxW = weight of the correction in the ordering, ThreatCtxUpd = scale of its update (/1024).
// ThreatHist 0 = no context (ctx is always 0: flat table only, bench 137419); 1-3 = factored form (3 is used by the
// tuning runs and tests, bench 270429).
//
// History: 16/10/2026, the earlier form with separate tables per context lost (H0, -3.32 +- 1.93 over 33,510 games at
// 8+0.08): its context repeated the lesser[] map of the fixed threat term, it split the updates of a move over four
// tables, and it also entered the reductions and the history pruning. Factored form after a short SPSA: -0.89 +- 4.25
// over 6,626 games. The separate-table forms (1, and 2 = any attacker) were removed on 17/10/2026.
static inline int qh_order(const ThreadData &td, int side, int key, int ctx) {
  const int16_t *h = td.quiet_hist[side];
  return h[key] + (ctx ? h[QH_SIZE + ((ctx - 1) << 12) + key] * ThreatCtxW / 1024 : 0);
}
static inline int qh_main(const ThreadData &td, int side, int key, int) {
  return td.quiet_hist[side][key];
}
static inline void qh_push(ThreadData &td, int side, int key, int ctx, int bonus, int grav) {
  int16_t *h = td.quiet_hist[side];
  hist_learn(h[key], bonus, LIM_QUIET, grav);
  if (ctx)
    hist_learn(h[QH_SIZE + ((ctx - 1) << 12) + key], bonus * ThreatCtxUpd / 1024, LIM_QUIET, grav);
}

// Is square sq threatened for our piece of type pt? Same definition as the maps of score_quiets, node occupancy.
static inline bool square_menaced(const ThreadData &td, int sq, int pt) {
  const int us = td.side, eoff = us == white ? 6 : 0;
  const U64 occ = td.occupancies[both];
  const U64 ep = td.bitboards[P + eoff], en = td.bitboards[N + eoff], eb = td.bitboards[B + eoff],
            er = td.bitboards[R + eoff];
  if (pawn_attacks[us][sq] & ep)
    return true;
  if (pt <= B)
    return false;
  if ((knight_attacks[sq] & en) || (get_bishop_attacks(sq, occ) & eb))
    return true;
  return pt == Q && (get_rook_attacks(sq, occ) & er);
}

// Threat context of one quiet move from its two squares (hash move before the maps exist, evasions).
static inline int quiet_ctx_slow(const ThreadData &td, int m) {
  if (!ThreatHist || get_move_promoted(m))
    return 0;
  const int pt = kPieceKind[move_piece(m)];
  if (pt == K)
    return 0;
  return (int)square_menaced(td, move_from(m), pt) | (int)square_menaced(td, move_to(m), pt) << 1;
}

// A quiet move learns in all its histories: main (with its threat-context correction), first plies, continuations,
// pawn structure. ex = expert of the node; HlAux scales the three auxiliary updates per expert.
static void learn_quiet(ThreadData &td, NodeFrame *f, int m, int bonus, int ctx, int ex) {
  const int key = quiet_key(m);
  const int grav = hist_grav(ex);
  const int aux = by_exp4(ex, HlAux0, HlAux1, HlAux2, HlAux3);
  qh_push(td, td.side, key, ctx, bonus, grav);
  if (f->ply < LOW_PLY_SIZE)
    hist_learn(td.low_ply_hist[f->ply][key], bonus * LowPlyScale / 1024 * aux / 1024, LIM_QUIET, grav);
  learn_followups(f, move_piece(m), move_to(m), bonus * ContUpdScale / 1024 * aux / 1024, ex);
  hist_learn(pawn_row(td)[move_piece(m)][move_to(m)],
             bonus * (bonus > -4 ? PawnBonusScale : PawnMalusScale) / 1024 * aux / 1024, LIM_PAWN, grav);
}

static inline void learn_capture(ThreadData &td, int m, int bonus, int ex) {
  hist_learn(td.capt_hist[move_piece(m)][move_to(m)][victim_kind(td, m)], bonus, LIM_CAPT, hist_cgrav(ex));
}

// ============================================================================
// CORRECTION OF THE STATIC EVALUATION (sub-block I of block 1)
// The tables learn the difference between the searched value and the static evaluation, by structure keys (pawns,
// minor pieces, non-pawns of each colour), by the pair of moves that led to the node, and optionally by expert.
// Ours in 8.1: the weight of the correction per expert (CorrGain), a quadratic depth term in its reward (CorrDepthQ,
// 13_search.inc) and the gravity per expert of the update (CorrGrav).
// ============================================================================

static inline CorrSlot &corr_slot(U64 key, int side_to_move) {
  return g_stats.corr[key & g_stats.corr_mask][side_to_move];
}

// CorrPhase (idea 9, 04/10/2026): with four experts the systematic error on one structure changes with the expert. A
// constant per band, mixed into the pawn and minor-piece keys, separates the corrections. (06/10/2026, speed, same
// values) The key is read from a table by number of pieces, without the comparisons of net_phase (~2.1 calls per node).
static constexpr auto corr_phase_keys_by_count = [] {
  constexpr U64 keys[4] = {0x9E3779B97F4A7C15ULL, 0xC2B2AE3D27D4EB4FULL, 0x165667B19E3779F9ULL, 0xD6E8FEB86659FD93ULL};
  std::array<U64, 65> t{};   // up to 64: even a FEN with too many pieces stays inside the table
  for (int c = 0; c <= 64; c++)
    t[c] = keys[c <= 9 ? 0 : c <= 15 ? 1 : c <= 23 ? 2 : 3];   // = net_phase(c)
  return t;
}();
// CorrPhaseQueen (17/10/2026, off = old tables; report R2 of the 8.1 rewrite): eight regimes instead of four, the piece
// band times "queens on the board". Inside a band the queens double the evaluation swings, so the systematic error on
// one structure differs with and without them. Same tables, a different key: with queens the band key is XORed with a
// fixed salt. Callers (prefetch in the make, corr_signal, corr_learn) all go through here, so they stay consistent.
static inline U64 corr_phase_key(const ThreadData &td) {
  if (!CorrPhase)
    return 0;
  const U64 k = corr_phase_keys_by_count[count_bits(td.occupancies[both])];
  return (CorrPhaseQueen && (td.bitboards[Q] | td.bitboards[q])) ? k ^ 0x6A09E667F3BCC909ULL : k;
}

// CorrFifty (ours, 05/10/2026, from an idea seen in Reckless): the systematic error on one structure changes with the
// distance from the fifty-move rule (the evaluation is damped by the counter, and in long endgames the position is
// worth something else). A constant per band of CorrFiftyStep plies (at most 16 bands), mixed into the pawn and
// non-pawn keys, separates the corrections. At 0 the key is the usual one.
static inline U64 corr_fifty_key(const ThreadData &td) {
  static constexpr U64 keys[16] = {0, 0x78FAC4B00841A538ULL, 0x5EB2C61E9C3747D9ULL, 0x59701BD515B6EE59ULL,
                                   0x2FEFFD3F283182A5ULL, 0x2DBE4F221F31A14FULL, 0xE19215DA308D5DA7ULL,
                                   0x4AA99EFB91350BC7ULL, 0xC9C3DD626F1AE010ULL, 0x642CD00D7AA43578ULL,
                                   0x4E3591F9E5FC0494ULL, 0x3FBCACD31257FB99ULL, 0x0C995B8887B93F26ULL,
                                   0x8408B9E4AF9644A9ULL, 0x10A6D573C576C901ULL, 0x5DDBAD22D85BAB7BULL};
  return CorrFiftyStep ? keys[std::min(td.fifty / CorrFiftyStep, 15)] : 0;
}

static int corr_signal(const ThreadData &td, const NodeFrame *f) {
  PROF_GUARD(prof_corr);
  const int us = td.side;
  const U64 phk = corr_phase_key(td);
  const U64 fk = corr_fifty_key(td);
  const int pawn = corr_slot(td.pawn_key ^ phk ^ fk, us).pawn;
  const int minor = corr_slot(td.mm_key[0] ^ phk, us).minor;
  const int npw = corr_slot(td.np_key[white] ^ fk, us).np_white;
  const int npb = corr_slot(td.np_key[black] ^ fk, us).np_black;
  const int prev = f[-1].move;
  int cont = CorrNoMoveW;   // fixed term after a null move (08/10/2026: a constant before, now a lever)
  if (move_ok(prev)) {
    const int to = move_to(prev), pc = td.piece_on[to];
    cont = CorrContW * ((*f[-2].cont_corr)[pc][to] + (*f[-4].cont_corr)[pc][to]);
    if (CorrCont6W)   // f[-6] always exists: there are 7 frames with the sentinel row before the root
      cont += CorrCont6W * (*f[-6].cont_corr)[pc][to];
    // CorrContOppW (the user's idea, 06/10/2026): the pair of the OPPONENT's last two moves (at -3 and -1), that is
    // its plan, instead of our move with its reply. Same table; the version with the last move alone, without
    // context, was closed (-7.6 +- 7.8 at 20+0.2).
    if (CorrContOppW)
      cont += CorrContOppW * (*f[-3].cont_corr)[pc][to];
  }
  // Weight of the correction per expert (CorrGain, /1024; 1024 = old value, s*1024/1024 = s). The tables were already
  // separate by expert band (CorrPhase); the weight was one for all.
  const int s = CorrPawnW * pawn + CorrMinorW * minor + CorrNonPawnW * (npw + npb) + cont;
  const int ex = net_phase(count_bits(td.occupancies[both]));
  return (int)((long long)s * by_exp4(ex, CorrGain0, CorrGain1, CorrGain2, CorrGain3) / 1024);
}

// CorrExpertW (06/10/2026): a pawn table per expert of the network, added to the shared corrections instead of
// replacing their key (as CorrPhase does). The shared table stays as the base; the per-expert one learns only the
// expert's own difference. The term enters the evaluation but NOT n.signal, which steers RFP, LMR and the singular
// extension, so that only accuracy is measured. At 0 the term is 0.
static inline U64 corr_expert_key(const ThreadData &td) {
  static constexpr U64 keys[4] = {0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL, 0x510E527FADE682D1ULL,
                                  0x9B05688C2B3E6C1FULL};
  const int pcs = count_bits(td.occupancies[both]);
  const int band = CorrExpertBands == 2 ? (pcs <= 15 ? 0 : 3) : net_phase(pcs);
  return td.pawn_key ^ keys[band];
}

static inline int corr_signal_extra(const ThreadData &td) {
  int s = 0;
  if (CorrExpertW)
    s += CorrExpertW * g_stats.corr_x[corr_expert_key(td) & g_stats.corr_mask][td.side];
  return s;
}

static inline int corrected_eval(int v, int signal) {
  return std::clamp(v + signal / 131072, -TB_WIN_FLOOR + 1, TB_WIN_FLOOR - 1);
}

// Update of the correction tables through hist_learn with the gravity of the node's expert (CorrGrav; 1024 = the old
// update, identical tree).
static void corr_learn(ThreadData &td, NodeFrame *f, int bonus, int ex) {
  PROF_GUARD(prof_corr);
  const int us = td.side;
  const int g = corr_grav(ex);
  const U64 phk = corr_phase_key(td);
  const U64 fk = corr_fifty_key(td);
  hist_learn(corr_slot(td.pawn_key ^ phk ^ fk, us).pawn, bonus * CorrPawnUpd / 128, LIM_CORR, g);
  hist_learn(corr_slot(td.mm_key[0] ^ phk, us).minor, bonus * CorrMinorUpd / 128, LIM_CORR, g);
  hist_learn(corr_slot(td.np_key[white] ^ fk, us).np_white, bonus * CorrNonPawnUpd / 128, LIM_CORR, g);
  hist_learn(corr_slot(td.np_key[black] ^ fk, us).np_black, bonus * CorrNonPawnUpd / 128, LIM_CORR, g);
  const int prev = f[-1].move;
  if (move_ok(prev)) {
    const int to = move_to(prev), pc = td.piece_on[to];
    hist_learn((*f[-2].cont_corr)[pc][to], bonus * CorrCont2Upd / 128, LIM_CORR, g);
    hist_learn((*f[-4].cont_corr)[pc][to], bonus * CorrCont4Upd / 128, LIM_CORR, g);
    if (CorrCont6W)
      hist_learn((*f[-6].cont_corr)[pc][to], bonus * CorrCont6Upd / 128, LIM_CORR, g);
    if (CorrContOppW)
      hist_learn((*f[-3].cont_corr)[pc][to], bonus * CorrContOppUpd / 128, LIM_CORR, g);
  }
  if (CorrExpertW)
    hist_learn(g_stats.corr_x[corr_expert_key(td) & g_stats.corr_mask][us], bonus * CorrExpertUpd / 128, LIM_CORR, g);
}
