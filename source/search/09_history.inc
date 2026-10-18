// search/09_history.inc -- Valutazione statica, statistiche delle mosse, correzione della valutazione.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

// ============================================================================
// VALUTAZIONE
// ============================================================================

// Comando "eval": valutazione statica della scacchiera globale (lato al tratto) e uscita grezza della rete. La rete
// legge i nostri bitboard cosi' come sono (07/10/2026, scacchiera unica v2).
int debug_eval_position() {
  return nn_eval(side == white, bitboards, occupancies, fifty);
}
int debug_eval_position_raw() {
  int raw = 0;
  nn_eval(side == white, bitboards, occupancies, fifty, &raw);
  return raw;
}

// Riparte dalla radice con la catena degli accumulatori della rete (a ogni iterazione). La rete legge la scacchiera
// del thread direttamente: non c'e' una sua copia da riallineare, solo la pila delle dirty da azzerare.
static inline void nn_root_sync(ThreadData &td) {
  nn_pos_set(td.nnpos);
#ifdef TRIUMV_VERIFY_NNSYNC
  td.nnstack->st[0].key = nn_piece_key(td);
#endif
}

// La rete valuta la scacchiera del thread. Prima: minacce e pedoni che mancano (nn_dirty_catch_up) e, con
// -DTRIUMV_VERIFY_NNSYNC, il controllo che la pila descriva questa scacchiera (06_nndirty.inc).
static inline int td_nn_eval(ThreadData &td) {
  {
    PROF_GUARD(prof_catchup);
    nn_dirty_catch_up(td);
  }
  nn_verify_sync(td);
  return nn_pos_eval(td.nnpos, td.bitboards, td.occupancies, td.piece_on, td.side == white, td.fifty);
}

// La rete smorza la valutazione col contatore delle 50 mosse: v * (199 - fifty) / 199. In TT si
// tiene il valore SENZA smorzamento, cosi' vale per ogni trasposizione, e lo si rismorza alla lettura.
// W1 (08/10/2026, velocita', stesso valore): il divisore 199 - fifty cambia a run time e la divisione era una idiv
// (~26 cicli di latenza) a ogni valutazione fresca, due volte (cache delle valutazioni e hash). Ora moltiplica per il
// reciproco M = ceil(2^40 / D) e tronca verso zero: esatto finche' |x| * (M*D - 2^40) < 2^40, cioe' |x| < 2^40 / 199;
// qui |x| = |v| * 199 < 2^28 (verificato con numpy per ogni fifty e |v| <= 2^20: 0 differenze). xperf 6 giri, PGO:
// cicli -0,42% mediogioco, +0,19% finali (nel rumore); tenuta perche' l'albero e' identico (docs/audit_8.0/W_*).
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
  // Lo stesso troncamento del ponte (nn_scale, nn_finalize: v - v*f/199), cosi' la stessa posizione vale uguale
  // fresca, dalla cache o dalla hash (05/10/2026, audit del nucleo; BAKE PULIZIA 06/10, opzione EvalRefadeBridge
  // ritirata la sera; prima v * (199 - fifty) / 199).
  return v - v * fifty / 199;
}

// Esperto della rete (fascia di materiale) per numero di pezzi: la stessa tabella di half_ka_v2_hm.h
// (<= 9, 10-15, 16-23, >= 24). Serve a PhaseEdge* e CorrPhase.
static inline int net_phase(int pieces) { return pieces <= 9 ? 0 : pieces <= 15 ? 1 : pieces <= 23 ? 2 : 3; }

// Valutazione della rete (unita' della rete).
static inline int net_evaluate(ThreadData &td) {
  PROF_GUARD(prof_eval);
  if (g_eval_off) {
    static const int mat[6] = {100, 320, 330, 500, 900, 0};
    int s = 0;
    for (int pc = 0; pc < 6; pc++)
      s += mat[pc] * (count_bits(td.bitboards[pc]) - count_bits(td.bitboards[pc + 6]));
    return td.side == white ? s : -s;
  }
  return td_nn_eval(td);   // E1 (15/10/2026): niente cache per thread, vedi threads.h
}

// Valutazione nelle unita' dei margini della ricerca: x EvalUnits/1024, dentro la fascia delle tablebase.
// (I termini dal residuo Res*, costanti sommate qui, sono stati tolti l'11/10/2026: vedi 01_params.inc.)
static inline int static_evaluate(ThreadData &td) {
  const int v = (int)((long long)net_evaluate(td) * EvalUnits / 1024);
  return std::clamp(v, -TB_WIN_FLOOR + 1, TB_WIN_FLOOR - 1);
}


// Correzioni apprese con chiave sui concetti del residuo (11/10/2026 sera): TUTTE CHIUSE e TOLTE, codice nei backup.
// CorrKingPawnW (tutti i pedoni + zona 2x2 dei re) -4,68 +- 7,19 su 2.378 a 8+0.08, nessun bug (Z3), chiave troppo fine;
// CorrShelterW (pedoni sulle 3 colonne attorno ai re, mediogioco) -5,63 +- 5,53 su 3.898 a 8+0.08 UHO;
// CorrPassKingW (passati + passati fuori dal quadrato del re nemico) -5,24 +- 8,49 su 744 nei finali 8+0.1.
// Grana misurata (posizioni per chiave in partita): pedoni 3,3 mg / 4,9 eg, scudo 4,6 mg, passati 15,8 / 6,8. Le
// correzioni esistenti sembrano prendere gia' il segnale apprendibile in partita: una tabella in piu' aggiunge rumore.
// Backup: _pre_corrkey2_2026-10-11, _pre_rimozione_shelter_2026-10-11, _pre_rimozione_passking_2026-10-11.

// Valore per lo schermo: dalle unita' della ricerca ai centipedoni (scala della rete e normalizzazione 400).
// SCALA 06/10/2026 (richiesta dell'utente): 449 -> 400, cosi' +1,00 = 50% di probabilita' di vincere. Taratura sui
// NOSTRI dati di autogioco (match_moe/fit_cp_scale.py): con 449 il 50% di vittorie cadeva a +0,89 stampato, piatto
// col materiale (86-95 da 17 a 78 punti di materiale) sia a 10+0.1 (563.496 posizioni) sia a 40+0.4 (99.438):
// basta una costante, non serve una curva per materiale. Solo stampa: la ricerca usa le unita' interne, il bench non
// cambia. Cambiano le soglie di aggiudicazione delle GUI (stessa posizione = cifra +12%).
static inline int units_to_cp(int v) {
  const long long net = (long long)v * 1024 / EvalUnits;
  const int es = nn_get_eval_scale();
  const long long cp = es == 100 ? net : net * 100 / es;
  return (int)(cp * 100 / 400);
}

// ============================================================================
// STATISTICHE DELLE MOSSE
// ============================================================================

// Valore di un tipo di pezzo nelle unita' dei margini (indice: P..K, poi "nessuno").
static const int unit_value[7] = {208, 781, 825, 1276, 2538, 0, 0};

// Every statistic of the engine (move histories, correction histories, hash-move history) is updated by
// hist_learn, below. The former update (stat_push, in the inherited form) was removed on 17/10/2026: hist_learn with
// gravity 1024 gives the same integer result.

// 12/10/2026 sera: i tre limiti sono parametri (HistLim*, 01_params.inc); nella release restano costanti.
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
// Tipo di promozione 0..4 (nessuna, N, B, R, Q): entra nella chiave delle statistiche quiete.
// 04/10/2026 sera: tabella sui 4 bit del campo al posto di `pr ? pr % 6 : 0` (stessi valori, anche per i codici che
// non si presentano mai): promo_kind e' chiamata per ogni mossa da quiet_key e is_tactical e valeva lo 0,6% dei cicli.
static constexpr signed char kPromoKind[16] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3};
static inline int  promo_kind(int m) { return kPromoKind[get_move_promoted(m)]; }
static inline int  quiet_key(int m) { return move_from(m) | (move_to(m) << 6) | (promo_kind(m) << 12); }
// Mossa "tattica": cattura o promozione a donna (codice Q = 4 o q = 10: bit 4 e 10 della maschera 0x410). Le altre
// promozioni si ordinano fra le quiete.
static inline bool is_tactical(int m) { return (get_move_capture(m) | ((0x410u >> get_move_promoted(m)) & 1)) != 0; }
// Tipo di pezzo P..K da un codice P..k, senza modulo (06/10/2026, velocita', stessi valori): `pc % 6` costava una
// moltiplicazione e tre operazioni per mossa (score_quiets, checks_before, victim_kind...). L'indice 15 e' "nessun
// pezzo" (-1 & 15) e da' CAPT_NONE, cosi' anche la casa vuota passa dalla tabella senza salto.
static constexpr signed char kPieceKind[16] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, CAPT_NONE, CAPT_NONE, CAPT_NONE, CAPT_NONE};
// Tipo del pezzo catturato (CAPT_NONE per l'en passant e per la promozione senza cattura, come la casa vuota).
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
// Origin: the saturating update and an amount linear in depth are known ideas (move histories, part of the
// algorithmic base of Stockfish 19). Our form: a gravity per expert, a quadratic term in depth, weights per expert.
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
//   (ContAgree*): values of ours from the 8.0 tuning (RW1);
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

// Idea 9 (CorrPhase, 04/10/2026): con quattro esperti l'errore sistematico su una stessa struttura cambia con
// l'esperto. Una costante per fascia mescolata nelle chiavi dei pedoni e dei minori separa le correzioni.
// 06/10/2026 (velocita', stessi valori): la chiave si legge da una tabella per numero di pezzi, senza i due o
// tre confronti di net_phase. Contatori: ~2,1 chiamate per nodo (prefetch nella make, corr_signal, corr_learn).
static constexpr auto corr_phase_keys_by_count = [] {
  constexpr U64 keys[4] = {0x9E3779B97F4A7C15ULL, 0xC2B2AE3D27D4EB4FULL, 0x165667B19E3779F9ULL, 0xD6E8FEB86659FD93ULL};
  std::array<U64, 65> t{};   // fino a 64: anche una FEN con troppi pezzi resta nella tabella
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

// CorrFifty (nostra, 05/10/2026, da un'idea di Reckless): l'errore sistematico della valutazione su una stessa
// struttura cambia con la distanza dalla regola delle 50 mosse (la rete smorza l'eval col contatore, e nei finali
// che si trascinano la posizione "vale" altro). Una costante per fascia di CorrFiftyStep semimosse (al massimo 16
// fasce) mescolata nelle chiavi dei pedoni e dei non-pedoni separa le correzioni. A 0 la chiave e' quella di sempre.
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
  int cont = CorrNoMoveW;   // termine fisso dopo la mossa nulla (08/10/2026: era la costante 64049, ora leva)
  if (move_ok(prev)) {
    const int to = move_to(prev), pc = td.piece_on[to];
    cont = CorrContW * ((*f[-2].cont_corr)[pc][to] + (*f[-4].cont_corr)[pc][to]);
    if (CorrCont6W)   // f[-6] esiste sempre: prima della radice ci sono 7 frame con la riga sentinella
      cont += CorrCont6W * (*f[-6].cont_corr)[pc][to];
    // CorrContOppW (idea dell'utente, 06/10/2026): la coppia delle due ultime mosse dell'AVVERSARIO (a -3 e a -1),
    // cioe' il suo piano, invece della nostra mossa con la sua risposta. Stessa tabella; la versione con la sola
    // ultima mossa, senza contesto, era chiusa (-7,6 +- 7,8 a 20+0.2).
    if (CorrContOppW)
      cont += CorrContOppW * (*f[-3].cont_corr)[pc][to];
  }
  // Weight of the correction per expert (CorrGain, /1024; 1024 = old value, s*1024/1024 = s). The tables were already
  // separate by expert band (CorrPhase); the weight was one for all.
  const int s = CorrPawnW * pawn + CorrMinorW * minor + CorrNonPawnW * (npw + npb) + cont;
  const int ex = net_phase(count_bits(td.occupancies[both]));
  return (int)((long long)s * by_exp4(ex, CorrGain0, CorrGain1, CorrGain2, CorrGain3) / 1024);
}

// CorrExpertW (06/10/2026, rapporto N_CORREZIONI 4.8): una tabella dei pedoni per esperto della rete che si somma
// alle correzioni condivise invece di sostituirne la chiave (come CorrPhase). La condivisa resta come base, quella per
// esperto impara solo la differenza propria dell'esperto. Il termine entra nella valutazione ma NON in n.signal, che
// regola RFP, LMR e singolare (rapporto 4.2): cosi' si misura solo la precisione. A 0 il termine vale 0.
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
