// search/09_history.inc -- Valutazione statica, statistiche delle mosse, correzione della valutazione.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

// ============================================================================
// VALUTAZIONE
// ============================================================================

static inline int nn_build_piece_list(const U64 *bbs, int pieces[33], int squares[33]) {
  int count = 0;
  for (int pc = P; pc <= k; pc++) {
    U64 bb = bbs[pc];
    while (bb) {
      const int sq = get_ls1b_index(bb);
      pieces[count] = nn_piece_code[pc];
      squares[count] = nnue_squares[sq];
      count++;
      pop_bit(bb, sq);
    }
  }
  return count;
}

// Comando "eval": valutazione statica della scacchiera globale (lato al tratto) e uscita grezza della rete.
int debug_eval_position() {
  int pieces[33], squares[33];
  const int count = nn_build_piece_list(bitboards, pieces, squares);
  return nn_eval(side == white, pieces, squares, count, fifty);
}
int debug_eval_position_raw() {
  int pieces[33], squares[33];
  const int count = nn_build_piece_list(bitboards, pieces, squares);
  int raw = 0;
  nn_eval(side == white, pieces, squares, count, fifty, &raw);
  return raw;
}

// Riallinea la posizione incrementale della rete alla scacchiera del thread (a ogni iterazione).
static inline void nn_root_sync(ThreadData &td) {
  int pieces[33], squares[33];
  const int count = nn_build_piece_list(td.bitboards, pieces, squares);
  nn_pos_set(td.nnpos, td.side == white, pieces, squares, count, td.fifty);
}

// La rete smorza la valutazione col contatore delle 50 mosse: v * (199 - fifty) / 199. In TT e nella cache si
// tiene il valore SENZA smorzamento, cosi' vale per ogni trasposizione, e lo si rismorza alla lettura.
static inline int eval_unfade(int v, int fifty) {
  if (fifty > 100) fifty = 100;
  return v * 199 / (199 - fifty);
}
static inline int eval_refade(int v, int fifty) {
  if (fifty > 100) fifty = 100;
  return v * (199 - fifty) / 199;
}

// Idea 1 (Disagree*, 04/10/2026): il disaccordo psqt - positional della rete viaggia con la valutazione. Spenta,
// la condizione e' falsa e nella release (parametri costanti a 0) il codice sparisce.
static inline bool disagree_on() { return (DisagreeRfp | DisagreeFut | DisagreeCapFut | DisagreeSing) != 0; }

// Esperto della rete (fascia di materiale) per numero di pezzi: la stessa tabella di half_ka_v2_hm.h
// (<= 9, 10-15, 16-23, >= 24). Serve a PhaseEdge* e CorrPhase.
static inline int net_phase(int pieces) { return pieces <= 9 ? 0 : pieces <= 15 ? 1 : pieces <= 23 ? 2 : 3; }

// Valutazione della rete (unita' della rete), con la cache per thread.
static inline int net_evaluate(ThreadData &td) {
  PROF_GUARD(prof_eval);
  if (g_eval_off) {
    static const int mat[6] = {100, 320, 330, 500, 900, 0};
    int s = 0;
    for (int pc = 0; pc < 6; pc++)
      s += mat[pc] * (count_bits(td.bitboards[pc]) - count_bits(td.bitboards[pc + 6]));
    td.eval_cplx = 0;
    return td.side == white ? s : -s;
  }
  ThreadData::EvalCacheEntry &ce = td.eval_cache[td.hash_key & ThreadData::EVAL_CACHE_MASK];
  if (ce.key == td.hash_key) {
    if (disagree_on())
      td.eval_cplx = ce.cplx;
    return eval_refade(ce.eval, td.fifty);
  }
  const int v = nn_pos_eval(td.nnpos, td.bitboards, td.occupancies);
  ce.key = td.hash_key;
  ce.eval = eval_unfade(v, td.fifty);
  if (disagree_on())
    ce.cplx = td.eval_cplx = nn_last_cplx(td.nnpos);
  return v;
}

// Valutazione nelle unita' dei margini della ricerca: x EvalUnits/1024, dentro la fascia delle tablebase.
static inline int static_evaluate(ThreadData &td) {
  int v = (int)((long long)net_evaluate(td) * EvalUnits / 1024);
  return std::clamp(v, -TB_WIN_FLOOR + 1, TB_WIN_FLOOR - 1);
}

// Valore per lo schermo: dalle unita' della ricerca ai centipedoni (scala della rete e normalizzazione 449).
static inline int units_to_cp(int v) {
  const long long net = (long long)v * 1024 / EvalUnits;
  const int es = nn_get_eval_scale();
  const long long cp = es == 100 ? net : net * 100 / es;
  return (int)(cp * 100 / 449);
}

// ============================================================================
// STATISTICHE DELLE MOSSE
// ============================================================================

// Valore di un tipo di pezzo nelle unita' dei margini (indice: P..K, poi "nessuno").
static const int unit_value[7] = {208, 781, 825, 1276, 2538, 0, 0};

// Spinge una statistica verso il bonus: piu' la voce e' gia' vicina al limite, meno si muove.
static inline void stat_push(int16_t &e, int bonus, int limit) {
  const int b = std::clamp(bonus, -limit, limit);
  const int v = e;
  e = (int16_t)(v + b - v * (b < 0 ? -b : b) / limit);
}
static inline void stat_push(int &e, int bonus, int limit) {
  const int b = std::clamp(bonus, -limit, limit);
  e += b - e * (b < 0 ? -b : b) / limit;
}

constexpr int LIM_QUIET  = 7183;
constexpr int LIM_CAPT   = 10692;
constexpr int LIM_FOLLOW = 30000;
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
// Tipo del pezzo catturato (CAPT_NONE per l'en passant e per la promozione senza cattura, come la casa vuota).
static inline int  victim_kind(const ThreadData &td, int m) {
  const int v = td.piece_on[move_to(m)];
  return v < 0 ? CAPT_NONE : v % 6;
}

static inline PieceToRow &pawn_row(const ThreadData &td) {
  return g_stats.pawn_rows[td.pawn_key & g_stats.pawn_mask];
}

// Statistiche di seguito: la mossa (pc, to) dopo le mosse fatte 1..6 ply prima. Pesi per distanza e un fattore
// che cresce con il numero di distanze che gia' la premiano.
static void learn_followups(NodeFrame *f, int pc, int to, int bonus) {
  static const int dist_weight[7] = {0, 520, 390, 145, 251, 66, 209};
  static const int agree_mul[7] = {94, 103, 110, 106, 119, 126, 121};
  int agreeing = 0;
  for (int d = 1; d <= 6; d++) {
    if (f->in_check && d > 2)
      break;
    if (!move_ok(f[-d].move))
      continue;
    int16_t &e = (*f[-d].cont_hist)[pc][to];
    if (e > 0)
      agreeing++;
    stat_push(e, bonus * dist_weight[d] * agree_mul[agreeing] / 65536 + (d < 2 ? 73 : 0), LIM_FOLLOW);
  }
}

static void learn_quiet(ThreadData &td, NodeFrame *f, int m, int bonus) {
  const int key = quiet_key(m);
  stat_push(td.quiet_hist[td.side][key], bonus, LIM_QUIET);
  if (f->ply < LOW_PLY_SIZE)
    stat_push(td.low_ply_hist[f->ply][key], bonus * LowPlyScale / 1024, LIM_QUIET);
  learn_followups(f, move_piece(m), move_to(m), bonus * ContUpdScale / 1024);
  stat_push(pawn_row(td)[move_piece(m)][move_to(m)],
            bonus * (bonus > -4 ? PawnBonusScale : PawnMalusScale) / 1024, LIM_PAWN);
}

static inline void learn_capture(ThreadData &td, int m, int bonus) {
  stat_push(td.capt_hist[move_piece(m)][move_to(m)][victim_kind(td, m)], bonus, LIM_CAPT);
}

// ============================================================================
// CORREZIONE DELLA VALUTAZIONE STATICA
// Si impara la differenza fra il valore cercato e la valutazione statica, per chiavi di struttura (pedoni, pezzi
// minori, non-pedoni di ciascun colore) e per la coppia di mosse che porta al nodo.
// ============================================================================

static inline CorrSlot &corr_slot(U64 key, int side_to_move) {
  return g_stats.corr[key & g_stats.corr_mask][side_to_move];
}

// Idea 9 (CorrPhase, 04/10/2026): con quattro esperti l'errore sistematico su una stessa struttura cambia con
// l'esperto. Una costante per fascia mescolata nelle chiavi dei pedoni e dei minori separa le correzioni.
static inline U64 corr_phase_key(const ThreadData &td) {
  static constexpr U64 keys[4] = {0x9E3779B97F4A7C15ULL, 0xC2B2AE3D27D4EB4FULL, 0x165667B19E3779F9ULL,
                                  0xD6E8FEB86659FD93ULL};
  return CorrPhase ? keys[net_phase(count_bits(td.occupancies[both]))] : 0;
}

static int corr_signal(const ThreadData &td, const NodeFrame *f) {
  PROF_GUARD(prof_corr);
  const int us = td.side;
  const U64 phk = corr_phase_key(td);
  const int pawn = corr_slot(td.pawn_key ^ phk, us).pawn;
  const int minor = corr_slot(td.mm_key[0] ^ phk, us).minor;
  const int npw = corr_slot(td.np_key[white], us).np_white;
  const int npb = corr_slot(td.np_key[black], us).np_black;
  const int prev = f[-1].move;
  int cont = 64049;
  if (move_ok(prev)) {
    const int to = move_to(prev), pc = td.piece_on[to];
    cont = CorrContW * ((*f[-2].cont_corr)[pc][to] + (*f[-4].cont_corr)[pc][to]);
  }
  int signal = CorrPawnW * pawn + CorrMinorW * minor + CorrNonPawnW * (npw + npb) + cont;
  if (CorrMajorW)
    signal += CorrMajorW * corr_slot(td.mm_key[1], us).major;
  return signal;
}

static inline int corrected_eval(int v, int signal) {
  return std::clamp(v + signal / 131072, -TB_WIN_FLOOR + 1, TB_WIN_FLOOR - 1);
}

static void corr_learn(ThreadData &td, NodeFrame *f, int bonus) {
  PROF_GUARD(prof_corr);
  const int us = td.side;
  const U64 phk = corr_phase_key(td);
  stat_push(corr_slot(td.pawn_key ^ phk, us).pawn, bonus, LIM_CORR);
  stat_push(corr_slot(td.mm_key[0] ^ phk, us).minor, bonus * CorrMinorUpd / 128, LIM_CORR);
  stat_push(corr_slot(td.np_key[white], us).np_white, bonus * CorrNonPawnUpd / 128, LIM_CORR);
  stat_push(corr_slot(td.np_key[black], us).np_black, bonus * CorrNonPawnUpd / 128, LIM_CORR);
  if (CorrMajorW)
    stat_push(corr_slot(td.mm_key[1], us).major, bonus * CorrMajorUpd / 128, LIM_CORR);
  const int prev = f[-1].move;
  if (move_ok(prev)) {
    const int to = move_to(prev), pc = td.piece_on[to];
    stat_push((*f[-2].cont_corr)[pc][to], bonus * 130 / 128, LIM_CORR);
    stat_push((*f[-4].cont_corr)[pc][to], bonus * 70 / 128, LIM_CORR);
  }
}
