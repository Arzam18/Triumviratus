// search/13_search.inc -- Ricerca principale.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.
//
// Un nodo passa per quattro fasi, ognuna una funzione: ingresso (patte, matti, hash, tablebase), valutazione e
// tagli prima delle mosse, ciclo delle mosse, apprendimento a fine nodo. Lo stato del nodo vive in NodeState.

// AN (09/10/2026): la ricerca esiste in due copie, partita (AN = false) e modalita' analisi (AN = true), scelte una
// volta alla radice (deepen). In partita ogni termine dell'analisi e' una costante: con i parametri Deep* congelati a 0
// della release il compilatore li toglie, come prima della modalita' analisi (xperf: la versione con i termini letti a
// ogni nodo costava ~1% di cicli in partita). Le due copie danno gli stessi valori di prima nei rispettivi modi.
template <bool AN>
static int explore(ThreadData &td, NodeFrame *f, int alpha, int beta, int depth, bool cut, bool pv);

#ifdef TRIUMV_REDLOG
#include "redlog.h"
#endif

constexpr int TRACKED = 32;   // mosse provate ricordate per i malus

struct NodeState {
  bool pv, root, cut, all;
  bool in_check, improving, opp_worse, tact_hash, prior_capture, hunt_mate;
  bool crystal;                   // AnCrystal: nodo della linea principale precedente vicino alla radice (crystal_node)
  bool no_early;                  // AnKingDanger / AnCycle: niente potature prima delle mosse (solo in analisi)
  bool no_lmr;                    // AnKingDanger / AnCycle: niente riduzioni LMR positive (solo in analisi)
  int  alpha, beta, depth;
  int  best, best_move, cap_value;
  int  moves;                     // mosse legali cercate (il contatore del frame lo riusa la ricerca singolare)
  int  eval, eval_raw, signal;
  int  signal_x;                  // correzioni sperimentali solo nella valutazione, non nella selettivita' (CorrExpertW)
  int  prev_to;
  int  excluded;
  int  prior_red;
  HashView h;
  int  quiets[TRACKED], n_quiets;
  unsigned char quiet_ctx[TRACKED];   // ThreatHist: contesto di minaccia di ciascuna quieta provata
  int  best_ctx;                  // ThreatHist: contesto della mossa migliore (0 se tattica)
  int  tacts[TRACKED], n_tacts;
  U64  causal;                    // CausalRed: case della confutazione dell'ultima quieta fallita bassa (0 = nessuna)
  int  expert;                   // esperto della rete per il materiale del nodo (net_phase), per i margini *Exp*
};

#ifdef TRIUMV_REDLOG
// Re-search data (redlog.h): the current move, already made, searched with a null window at lo, lo+1 and lo+2.
template <bool AN>
static void redres_probes(ThreadData &td, NodeFrame *f, const NodeState &n, int type, int d, int nd, int lo,
                          int value, int move_no, int r, int flags) {
  RedresRec rec{};
  rec.type = (uint8_t)type;
  rec.d = (int8_t)d;
  rec.nd = (int8_t)nd;
  rec.depth = (int16_t)n.depth;
  rec.moves = (int16_t)n.moves;
  rec.move_no = (int16_t)move_no;
  rec.expert = (int16_t)n.expert;
  rec.ply = (int16_t)f->ply;
  rec.stat_score = f->stat_score;
  rec.r = r;
  rec.signal = n.signal;
  rec.hash_known = val_known(n.h.value);
  rec.hash_gap = rec.hash_known ? redres_clip(n.h.value - f->static_eval) : 0;
  rec.flags = (uint16_t)flags;
  rec.m_best = redres_clip(value - n.best);
  rec.m_alpha = redres_clip(value - n.alpha);
  // The probes share the hash table, so a later probe is cheaper than it would be alone: the order is drawn from six
  // permutations and stored in bits 3-5 of done, and the fit reads the cost of a depth only when it ran first.
  static const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  const int perm = int(redlog_counters[td.thread_id * 16 + 3]++ % 6);
  rec.done = uint8_t(perm << 3);
  for (int j = 0; j < 3; j++) {
    const int k = perms[perm][j], depth = lo + k;
    if (depth < 1)
      continue;
    const U64 before = td.nodes;
    const int v = -explore<AN>(td, f + 1, -(n.alpha + 1), -n.alpha, depth, !n.cut, false);
    if (stop_threads.load(std::memory_order_relaxed))
      return;
    rec.v[k] = redres_clip(v - n.alpha);
    rec.nodes[k] = (uint32_t)std::min<U64>(td.nodes - before, 0xffffffffu);
    rec.done |= uint8_t(1 << k);
  }
  redres_write(td.thread_id, rec);
}
#endif

// Margini per esperto (10/10/2026, nostra): Consilium valuta con quattro esperti per fascia di materiale (net_phase),
// e la fiducia nella valutazione statica non e' la stessa nei quattro. Le leve RfpExp*, FutExp*, LmrExp* spostano
// il margine della futility inversa, quello della futility delle quiete e la riduzione LMR secondo l'esperto del nodo
// (estensione delle correzioni per esperto, CorrPhase +3,2). Tutte a 0 = albero invariato; nella release, con i
// parametri costanti a 0, il compilatore toglie anche il calcolo dell'esperto.
static inline int by_expert(int e, int v0, int v1, int v2, int v3) {
  return e == 0 ? v0 : e == 1 ? v1 : e == 2 ? v2 : v3;
}

// CausalRed (09/10/2026, idea di giugno "potatura causale", riscritta sulla ricerca attuale). Quando una nostra mossa
// quieta fallisce bassa, la mossa dell'avversario che la confuta (il taglio beta nel figlio) dice dove sta il problema:
// la casa da cui parte, quella dove arriva e le case che il pezzo attacca da li'. Le quiete sorelle provate dopo che
// non toccano nessuna di queste case (ne' partenza ne' arrivo) di solito non cambiano nulla contro quella confutazione,
// e si riducono di piu'. Mai potate: la ricerca ridotta che supera alpha si ripete a profondita' piena come sempre.
// Gli scacchi, la mossa hash e le spinte dei passati restano fuori. Precedente noto: la "minaccia" della mossa nulla
// usata da Stockfish 1.x-2.x per potare le mosse non collegate (connected_threat); qui la minaccia e' la confutazione
// delle sorelle e la mossa si riduce, non si pota.
// Case attaccate dal pezzo pc (codice P..k) dalla casa sq con l'occupazione occ.
static inline U64 piece_attacks(int pc, int sq, U64 occ) {
  switch (pc % 6) {
    case 0: return pawn_attacks[pc >= 6 ? black : white][sq];
    case 1: return knight_attacks[sq];
    case 2: return get_bishop_attacks(sq, occ);
    case 3: return get_rook_attacks(sq, occ);
    case 4: return get_queen_attacks(sq, occ);
    default: return king_attacks[sq];
  }
}
static inline U64 causal_mask(const ThreadData &td, int refute) {
  const int from = get_move_source(refute), to = get_move_target(refute), pc = get_move_piece(refute);
  return piece_attacks(pc, to, td.occupancies[both]) | (1ULL << from) | (1ULL << to);
}

static constexpr int NO_VALUE = 1 << 30;   // "nessun valore": la fase non ha chiuso il nodo

// Reciproci a 40 bit di 256*depth + LmrAllDivBase (divisore della riduzione ai nodi all, vedi move_loop): M =
// ceil(2^40 / D). 12/10/2026 sera: con LmrAllDivBase e HistDiv* parametri, nella release le tabelle restano costanti
// calcolate in compilazione; nel dev si ricalcolano a ogni setoption e all'avvio (param_tables_refresh).
static constexpr auto make_all_node_recip = [](int base) {
  std::array<unsigned long long, max_ply + 8> r{};
  for (int d = 0; d < max_ply + 8; d++) {
    const unsigned long long D = 256ULL * d + (unsigned long long)base;
    r[d] = ((1ULL << 40) + D - 1) / D;
  }
  return r;
};
static constexpr auto make_hist_recip = [](const std::array<int, 16> &div) {
  std::array<unsigned long long, 16> r{};
  for (int i = 0; i < 16; i++)
    r[i] = ((1ULL << 40) + (unsigned long long)div[i] - 1) / (unsigned long long)div[i];
  return r;
};
#define HIST_DIV_LIST {HistDiv1, HistDiv2, HistDiv3, HistDiv4, HistDiv5, HistDiv6, HistDiv7, HistDiv8, HistDiv9, \
                       HistDiv10, HistDiv11, HistDiv12, HistDiv13, HistDiv14, HistDiv15, HistDiv16}
#ifdef TRIUMV_RELEASE
static constexpr std::array<int, 16> hist_div = HIST_DIV_LIST;
static constexpr auto all_node_recip = make_all_node_recip(LmrAllDivBase);
static constexpr auto hist_recip = make_hist_recip(hist_div);
#else
static std::array<int, 16> hist_div;
static std::array<unsigned long long, max_ply + 8> all_node_recip;
static std::array<unsigned long long, 16> hist_recip;
static void red_tables_refresh();   // block 2, below
static void param_tables_refresh() {
  red_tables_refresh();
  hist_div = HIST_DIV_LIST;
  all_node_recip = make_all_node_recip(LmrAllDivBase);
  hist_recip = make_hist_recip(hist_div);
}
static const bool param_tables_ready = (param_tables_refresh(), true);
#endif
#undef HIST_DIV_LIST
// W1 (08/10/2026): reciproci di depth + 1 (media del valore di taglio in node_close), stesso troncamento.
static constexpr auto succ_recip = [] {
  std::array<unsigned long long, max_ply + 8> r{};
  for (int d = 0; d < max_ply + 8; d++)
    r[d] = ((1ULL << 40) + (unsigned long long)d) / (unsigned long long)(d + 1);
  return r;
}();

// ---------------------------------------------------------------------------
// Rampa "tempo lungo" (05/10/2026). Ogni leva ha uno scostamento Lt*: il valore effettivo e' base + Lt * peso, con il
// peso (0..1024) fissato per iterazione dalla profondita' di radice (lt_update, 14_deepen.inc). Con lo scostamento a 0
// l'accessor restituisce la leva base: nella release (costanti) il ramo sparisce e il codice e' quello di prima.
// ---------------------------------------------------------------------------
static inline int lt_value(int base, int offset, int w) { return base + offset * w / 1024; }

static inline void lt_update(ThreadData &td) {
  td.lt_w = std::clamp((td.root_depth - LtKnee) * 1024 / LtSpan, 0, 1024);
  const int w = td.lt_w;
  td.lt_lmr_not_improving = lt_value(LmrNotImproving, LmrNotImprovingLt, w);
  td.lt_lmr_offset        = lt_value(LmrOffset, LmrOffsetLt, w);
  td.lt_lmr_all_node      = lt_value(LmrAllNode, LmrAllNodeLt, w);
  td.lt_lmr_no_tt_full    = lt_value(LmrNoTTFull, LmrNoTTFullLt, w);
  td.lt_nmp_improving     = lt_value(NmpImproving, NmpImprovingLt, w);
  td.lt_quiet_fut_alpha   = lt_value(QuietFutAlpha, QuietFutAlphaLt, w);
  td.lt_rfp_max_depth     = lt_value(RfpMaxDepth, RfpMaxDepthLt, w);
  td.lt_nmp_verify_depth  = lt_value(NmpVerifyDepth, NmpVerifyDepthLt, w);
  if (LmrLogMulLt) {
    const int mul = lt_value(LmrLogMul, LmrLogMulLt, w);
    if (mul != td.lt_lmr_log_mul) {   // stessa formula di build_reduction_table, col moltiplicatore effettivo
      td.lt_lmr_log_mul = mul;
      td.lt_red_log[0] = 0;
      for (int i = 1; i < 256; i++)
        td.lt_red_log[i] = int(mul / 128.0 * std::log(double(i)));
    }
  }
}
static inline int e_lmr_not_improving(const ThreadData &td) { return LmrNotImprovingLt ? td.lt_lmr_not_improving : LmrNotImproving; }
static inline int e_lmr_offset(const ThreadData &td)        { return LmrOffsetLt ? td.lt_lmr_offset : LmrOffset; }
static inline int e_lmr_all_node(const ThreadData &td)      { return LmrAllNodeLt ? td.lt_lmr_all_node : LmrAllNode; }
static inline int e_lmr_no_tt_full(const ThreadData &td)    { return LmrNoTTFullLt ? td.lt_lmr_no_tt_full : LmrNoTTFull; }
static inline int e_nmp_improving(const ThreadData &td)     { return NmpImprovingLt ? td.lt_nmp_improving : NmpImproving; }
static inline int e_quiet_fut_alpha(const ThreadData &td)   { return QuietFutAlphaLt ? td.lt_quiet_fut_alpha : QuietFutAlpha; }
static inline int e_rfp_max_depth(const ThreadData &td)     { return RfpMaxDepthLt ? td.lt_rfp_max_depth : RfpMaxDepth; }
// Sempre il valore del thread (09/10/2026): lt_update lo calcola per iterazione e la modalita' analisi (AN_NMPV) lo
// sovrascrive; letto solo dopo una mossa nulla riuscita, quindi senza costo. In partita vale NmpVerifyDepth.
static inline int e_nmp_verify_depth(const ThreadData &td)  { return td.lt_nmp_verify_depth; }
static inline const int *e_red_log(const ThreadData &td)    { return LmrLogMulLt ? td.lt_red_log : red_log; }

// Profondita' ridotta di base: tabella logaritmica, finestra relativa e assenza di miglioramento.
// `window_term` = finestra * LmrDeltaMul / root_delta: lo calcola move_loop solo quando la finestra cambia, perche'
// la divisione per root_delta (non costante) costava ~30 cicli a ogni mossa (04/10/2026, stesso valore).
static inline int base_reduction(const ThreadData &td, bool improving, int depth, int move_no, int window_term) {
  const int *rl = e_red_log(td);
  const int scale = rl[std::min(depth, 255)] * rl[std::min(move_no, 255)];
  return scale - window_term + (!improving ? scale * e_lmr_not_improving(td) / 512 : 0) + LmrBase;
}

// ---------------------------------------------------------------------------
// Block 2 of 8.1: late-move reduction of our own (RedForm = 1; 0 keeps the previous formula). Units of 1/1024 ply.
//
//   base   a power law of the depth d and the move number m with an amplitude per expert of the network:
//          A[e] * max(d + d0, 0.5)^alpha * max(m + m0, 0.5)^beta, two tables built from the parameters.
//   inner  base * (4096 + trust) / 4096 + facts + moves / 16
//          trust: how far the node can be believed (hash on the principal variation and its value and depth, cut
//          node, hash capture, no hash move, improving, the opponent worsening, size of the correction) scales the
//          base; the same features also add a fixed amount (facts); the move terms add the quality of the move
//          (statistic score, check, capture, children cutoffs, quiet gap between alpha and the evaluation, passed
//          push, causal term, move number, window, depth).
//   final  in all nodes the whole value grows by RfAllScale / sqrt(d), a share that fades with depth.
// The pruning rules read a lighter value (red_entry_ours): its own power law scaled by improving, plus the hash on
// the principal variation, the move number, the window and a constant.
// Every value came from a least-squares fit on 20.7 million moves of our own searches (tools/redfit_engine.py,
// 18/10/2026): 92% of the reduced moves are searched at the same depth as with the previous formula, all of them
// within one ply; the pruning value gives the same depth on 88%. Refit on 18/10 on the engine with block 1 reverted
// (26.5 million moves). The window enters as window * 1024 / root_delta (red_window_ours in move_loop).
// Baked on 18/10 and taken back out on 19/10 (user): 8.1 searched 0.35 ply less deep than 8.0 at the same time and
// lost; block 2 returns to the tree of 8.0 and each change is measured again on its own, in order.
// ---------------------------------------------------------------------------
static int rf_dp[256], rf_mp[256], re_dp[256], re_mp[256];   // 1024 * max(x + x0, 0.5)^exponent
static int rf_isqrt[256];                                     // 1024 / sqrt(d)
static void red_tables_refresh() {
  const auto fill = [](int *t, int x0_tenths, int exp_thousandths) {
    for (int i = 0; i < 256; i++)
      t[i] = int(std::lround(1024.0 * std::pow(std::max(i + x0_tenths / 10.0, 0.5), exp_thousandths / 1000.0)));
  };
  fill(rf_dp, RfD0, RfAlpha);
  fill(rf_mp, RfM0, RfBeta);
  fill(re_dp, ReD0, ReAlpha);
  fill(re_mp, ReM0, ReBeta);
  for (int i = 0; i < 256; i++)
    rf_isqrt[i] = int(std::lround(1024.0 / std::sqrt(double(std::max(i, 1)))));
}
static const bool red_tables_ready = (red_tables_refresh(), true);

static inline int red_entry_ours(const NodeFrame *f, const NodeState &n, int move_no, int window_term) {
  const int d = std::min(n.depth, 255), m = std::min(move_no, 255);
  const int base = ((by_expert(n.expert, ReA0, ReA1, ReA2, ReA3) * re_dp[d]) >> 10) * re_mp[m] >> 10;
  return base * (1024 + ReImproving * n.improving) / 1024 + ReConst + ReMoveNo * m + ReWindow * window_term / 16 +
         ReTtPv * f->tt_pv;
}

static inline int red_final_ours(const NodeFrame *f, const NodeState &n, int move_no, bool tact, bool gives,
                                 bool passer, bool causal_hit, int window_term) {
  const int d = std::min(n.depth, 255), m = std::min(move_no, 255);
  const int base = ((by_expert(n.expert, RfA0, RfA1, RfA2, RfA3) * rf_dp[d]) >> 10) * rf_mp[m] >> 10;
  const int tp = f->tt_pv, above = n.h.value > n.alpha, deep = n.h.depth >= n.depth, nohash = !n.h.move;
  const int cut = n.cut, corr = std::abs(n.signal) >> 16;   // correction size, up to about 1000
#define RF_NODE(W) (W##TtPv * tp + W##TtPvPv * (tp & n.pv) + W##TtPvAbove * (tp & above) + W##TtPvDeep * (tp & deep) + \
                    W##TtPvDeepCut * (tp & deep & cut) + W##Cut * cut + W##CutNoHash * (cut & nohash) +              \
                    W##HashCapt * n.tact_hash + W##NoHash * nohash + W##Improving * n.improving +                    \
                    W##OppWorse * n.opp_worse)
  // Trust per 4096 of the base, the correction size per 256 in trust and per 16 in facts: finer steps than whole
  // units, since a rounded weight on a feature that reaches ~1000 shifted the deep nodes by a tenth of a ply.
  const int trust = RF_NODE(RfTrust) + RfTrustCorr * corr / 256, facts = RF_NODE(RfAdd) + RfAddCorr * corr / 16;
#undef RF_NODE
  const int cuts = f[1].cutoff_cnt;
  const int gap = (tact || val_decided(n.alpha)) ? 0 : std::clamp(n.alpha - n.eval, -64, 96);
  const int moves = RfStat * (f->stat_score >> 6) + RfCheck * gives + RfCapture * tact + RfCutoffs * (cuts > 1) +
                    RfCutoffsMany * (cuts > 2) + RfGap * gap + RfPasser * passer + RfCausal * causal_hit +
                    RfMoveNo * m + RfWindow * window_term + RfConst + RfAllCutoffs * (n.all && cuts > 1) + RfDepth * d;
  // RfVolatile (ours, off): a node whose hash value is far from its corrected static evaluation is in motion, so its late
  // moves are reduced by a weight per 16 units of that distance (at most 64 steps).
  const int volatile_steps = (RfVolatile && val_known(n.h.value) && !val_decided(n.h.value))
                                 ? std::min(std::abs(n.h.value - f->static_eval) >> 4, 64) : 0;
  const int inner = base * (4096 + trust) / 4096 + facts + moves / 16 + RfVolatile * volatile_steps;
  return n.all ? inner * (1024 + ((RfAllScale * rf_isqrt[d]) >> 10)) / 1024 : inner;
}

// ---------------------------------------------------------------------------
// Idee nostre del 04/10/2026 (docs/audit_8.0/M_STUDIO_IDEE_RICERCA.md). Tutte spente di default: con i parametri
// a 0 i termini valgono 0 e nella release (parametri costanti) il compilatore li toglie.
// ---------------------------------------------------------------------------

// Idea 2 (Deep*): scostamento "profondo" pesato da td.deep_w (0..1024, cresce con la profondita' di radice).
// Dal 09/10/2026 le sei leve che la modalita' analisi usa (Rfp, Fut, Lmr, Nmp, Sing, Ext) arrivano gia' pesate nei
// campi td.dz_* (deepen, per iterazione): stesso valore di prima, ma la modalita' analisi le puo' accendere anche
// nella release, dove i parametri sono costanti. deep_term resta per DeepHist e DeepTtPv.
static inline int deep_term(const ThreadData &td, int offset) { return offset * td.deep_w / 1024; }

// Crystal (AnCrystal, modalita' analisi): il nodo sta sulla linea principale dell'iterazione precedente, vicino
// alla radice? In partita crystal_ply = -1 e il test e' sempre falso.
static inline bool crystal_node(const ThreadData &td, const NodeFrame *f) {
  return f->ply <= td.crystal_ply && f->follow_pv;
}

// Re in pericolo (AnKingDanger, Pos Solver, 14/10/2026; idea di Crystal, codice nostro): almeno 3 delle case intorno
// al re del lato `s` sono attaccate dall'avversario. Fino a 8 controlli di attacco: si calcola solo in analisi con il
// componente acceso.
// AnNmpZug: al massimo 5 mosse legali, come Crystal. Prima versione con le pseudo-legali (14/10/2026 notte): non
// scattava quasi mai (1 posizione su 383 con la ricerca diversa). Chiamata solo fuori scacco (mossa nulla), dove
// legal_before vale.
// The same count serves the endgame rule NmpFewMoves (18/10/2026) with its own limit.
static bool few_moves(ThreadData &td, int limit = 5) {
  moves ml;
  td_generate_moves(td, &ml);
  MoveSafety s;
  s.ready = false;
  int legal = 0;
  for (int i = 0; i < ml.count; i++)
    if (legal_before(td, s, ml.moves[i]) && ++legal > limit)
      return false;
  return true;
}

static bool king_in_danger(ThreadData &td, int s) {
  const int ksq = get_ls1b_index(td.bitboards[s == white ? K : k]);
  U64 zone = king_attacks[ksq];
  int hit = 0;
  while (zone) {
    const int sq = get_ls1b_index(zone);
    zone &= zone - 1;
    if (td_is_square_attacked(td, sq, s ^ 1) && ++hit >= 3)
      return true;
  }
  return false;
}

// GoldDigger (AnGold, idea di ShashChess/amchess, codice nostro): la mossa nulla si salta quando il "calore" tattico
// arriva a 4. La parte della radice (re in pericolo o sacrificio possibile +10, posizione di vantaggio netto +2,
// attacco +2, pedone avanzato +2, piu' di 30 mosse legali +1) e' in td.gd_heat; qui si aggiungono il materiale non
// pedoni di chi muove (> 2000, +1) e la profondita' (> 8 e > 12, +1 ciascuna). In partita gd_heat = -1000.
static inline bool gd_skip_null(const ThreadData &td, const NodeState &n) {
  if (td.gd_heat < -100)
    return false;
  const int off = td.side == white ? 0 : 6;
  int npm = 0;
  for (int t = N; t <= Q; t++)
    npm += unit_value[t] * count_bits(td.bitboards[t + off]);
  return td.gd_heat + (npm > 2000) + (n.depth > 8) + (n.depth > 12) >= 4;
}

// Controllo del tempo e dei limiti (solo il thread 0, ogni 512 nodi).
static int g_poll_countdown = 0;
static void poll_limits(ThreadData &td) {
  if (td.thread_id != 0 || --g_poll_countdown > 0)
    return;
  g_poll_countdown = g_limits.nodes ? (int)std::min<U64>(512, g_limits.nodes / 1024 + 1) : 512;
  if (g_pondering.load(std::memory_order_relaxed))
    return;
  const int elapsed = get_time_ms() - g_limits.start_ms;
  extern int clock_hard_limit();
  U64 total = 0;
  if (g_limits.nodes)
    for (auto &t : thread_data) total += t.nodes;
  if ((g_limits.use_tm() && (elapsed > clock_hard_limit() || g_stop_at_ponderhit.load(std::memory_order_relaxed)) &&
       root_move_ready()) ||
      (g_limits.movetime && elapsed >= g_limits.movetime && root_move_ready()) ||
      (g_limits.nodes && total >= g_limits.nodes))
    stop_threads.store(true, std::memory_order_relaxed);
}

// Statistiche di un nodo che ha trovato la mossa migliore: premio a lei, malus alle mosse provate prima.
static void reward_best(ThreadData &td, NodeFrame *f, NodeState &n) {
  PROF_GUARD(prof_hist);
  const int bm = n.best_move;
  int bonus = hist_amount(n.expert, n.depth, BonusMul, BonusSub, BonusMax) + (bm == n.h.move ? BonusTTMove : 0) +
              f[-1].stat_score / BonusPrevStatDiv;
  const int malus = hist_amount(n.expert, n.depth, MalusMul, MalusSub, MalusMax);
  if (!n.pv)
    bonus += (int)((long long)bonus * (n.n_quiets + n.n_tacts) / 256);

  // E. How much the histories learn (HistLearn, block 1): reward, penalty and penalty decay of quiet moves, reward and
  //    penalty of captures, per expert. In an endgame a quiet move that cuts among few alternatives says more than one
  //    among forty in the opening. Defaults = the old single scales.
  const int qb = by_expert(n.expert, HlQBonus0, HlQBonus1, HlQBonus2, HlQBonus3);
  const int qm = by_expert(n.expert, HlQMalus0, HlQMalus1, HlQMalus2, HlQMalus3);
  const int qd = by_expert(n.expert, HlQDecay0, HlQDecay1, HlQDecay2, HlQDecay3);
  const int cb = by_expert(n.expert, HlCBonus0, HlCBonus1, HlCBonus2, HlCBonus3);
  const int cm = by_expert(n.expert, HlCMalus0, HlCMalus1, HlCMalus2, HlCMalus3);
  if (!is_tactical(bm)) {
    learn_quiet(td, f, bm, bonus * qb / 1024, n.best_ctx, n.expert);
    int m = malus * qm / 1024;
    for (int i = 0; i < n.n_quiets; i++) {
      m = m * qd / 1024;
      learn_quiet(td, f, n.quiets[i], -m, n.quiet_ctx[i], n.expert);
    }
  } else
    learn_capture(td, bm, bonus * cb / 1024, n.expert);

  // La mossa che ha portato qui, se quieta e cercata subito (prima mossa, o seconda dopo la mossa hash), era debole.
  if (n.prev_to >= 0 && f[-1].move_count == 1 + (f[-1].tt_hit ? 1 : 0) && !n.prior_capture)
    learn_followups(f - 1, td.piece_on[n.prev_to], n.prev_to, -malus * PrevContMalus / 1024, n.expert);

  for (int i = 0; i < n.n_tacts; i++)
    learn_capture(td, n.tacts[i], -malus * cm / 1024, n.expert);
}

// --------------------------------------------------------------------------
// Fase 1: ingresso del nodo. Ritorna un valore se il nodo si chiude qui.
// --------------------------------------------------------------------------
static int node_open(ThreadData &td, NodeFrame *f, NodeState &n) {
  // La soglia e' il valore della patta per chi muove (0 senza Contempt): cosi' alpha non scende mai (05/10/2026,
  // audit dei porting: con la soglia 0 Contempt abbassava alpha dal lato di radice).
  if (!n.root && n.alpha < contempt(td) && td_upcoming_repetition(td)) {
    n.alpha = draw_score(td);
    if (n.alpha >= n.beta)
      return n.alpha;
  }
  f->move_count = 0;
  f->follow_pv = n.root || (f[-1].follow_pv && td.ply - 1 < td.last_pv_len && f[-1].move == td.last_pv[td.ply - 1]);
  poll_limits(td);
  if (n.pv && td.sel_depth < td.ply + 1)
    td.sel_depth = td.ply + 1;

  if (!n.root) {
    if (stop_threads.load(std::memory_order_relaxed) || td.ply >= max_ply || drawn_here(td, n.in_check))
      return (td.ply >= max_ply && !n.in_check) ? static_evaluate(td) : draw_score(td);
    // BAKE INSUFMATERIAL (11/10/2026, agente AE): materiale insufficiente dopo una cattura (12_quiesce.inc).
    if (f[-1].captured >= 0 && insufficient_material(td))
      return draw_score(td);
    n.alpha = std::max(mated_at(td.ply), n.alpha);
    n.beta = std::min(mating_in(td.ply + 1), n.beta);
    if (n.alpha >= n.beta)
      return n.alpha;
  }

  n.prev_to = move_ok(f[-1].move) ? move_to(f[-1].move) : -1;
  n.prior_red = f[-1].reduction;
  f[-1].reduction = 0;
  f->stat_score = 0;
  f[2].cutoff_cnt = 0;
  n.signal = corr_signal(td, f);
  n.signal_x = corr_signal_extra(td);
  n.excluded = f->excluded;
  n.expert = net_phase(count_bits(td.occupancies[both]));

  hash_read(td, n.h);
  f->tt_hit = n.h.hit;
  if (n.root)
    n.h.move = td.root[td.pv_idx].pv[0];
  if (!n.excluded)
    f->tt_pv = n.pv || (n.h.hit && n.h.pv);
  n.tact_hash = n.h.move && is_tactical(n.h.move);
  return NO_VALUE;
}

// --------------------------------------------------------------------------
// CUTOFF FROM THE HASH (block 5 in our form, 17/10/2026)
// An entry ends the node when it is deep enough and its bound settles the comparison with beta. In order:
//   1. depth: an upper bound (value <= beta) may lack HashLowSlack plies; any other entry must be deeper than the node;
//   2. node type: an entry cuts on the side the node expects (cut node and fail high, or the reverse), and on both
//      sides above depth HashAnyNode;
//   3. learning: a quiet hash move that fails high is rewarded, the previous move is penalized if it came early;
//   4. guards: no cutoff close to the fifty-move rule (HashFiftyGuard); from depth HashVerify the child of the hash
//      move, if it is in the table, must agree with the cutoff;
//   5. the returned value: a lower bound is pulled toward beta (HashDamp, ours after an idea seen in Coda);
//   6. an entry deep enough whose bound contradicts the window loses a ply above depth HashWeaken.
// Defaults = the 8.0 tree.
// --------------------------------------------------------------------------
static inline int hash_cut_value(const NodeState &n) {
  const int v = n.h.value;
  if (HashDamp && n.h.bound == BOUND_LOW && v >= n.beta && !val_decided(v))
    return (HashDamp * v + 10 * n.beta) / (HashDamp + 10);
  return v;
}

// Does the child of the hash move, when the table knows it, confirm the side of beta the entry claims?
static bool hash_child_agrees(ThreadData &td, const NodeState &n) {
  const HashView &h = n.h;
  MoveSafety s;
  s.ready = false;
  if (!legal_before(td, s, h.move))
    return true;
  UndoInfo u;
  td.repetition_table[++td.repetition_index] = td.hash_key;
  td.ply++;
  bool agrees = true;
  if (td_make_move(td, h.move, u)) {
    int cm, cv, cd, cf, ce;
    bool cp;
    const bool known = probe_tt(tt_key(td), cm, cv, cd, cf, ce, cp) && cf != BOUND_NIL;
    const int child = known ? from_hash_value(cv, td.ply, td.fifty) : VAL_NONE;
    td_unmake_move(td, h.move, u);
    agrees = !val_known(child) || (h.value >= n.beta) == (-child >= n.beta);
  }
  td.ply--;
  td.repetition_index--;
  return agrees;
}

static int hash_cutoff(ThreadData &td, NodeFrame *f, NodeState &n) {
  const HashView &h = n.h;
  const int need = n.depth - (h.value <= n.beta ? HashLowSlack : 0);
  const bool deep = h.depth > need;
  // HashNearMiss (an entry one ply short that cuts beyond a margin, seen in Coda): off. Removed on 05/10/2026 (-6.7 +-
  // 10.6 over 1,142 games at 20+0.2) and put back off on 12/10 for a second look; 0 = old tree.
  if (HashNearMiss && !deep && !n.pv && !n.cut && !n.excluded && h.depth == need && td.fifty < 90 &&
      h.bound == BOUND_UP && val_known(h.value) && !val_decided(h.value) && h.value + HashNearMiss <= n.alpha)
    return h.value + HashNearMiss;
  if (n.pv || n.excluded || !deep || !val_known(h.value))
    return NO_VALUE;
  const bool high = h.value >= n.beta;
  if (!bound_covers(h.bound, high) || (n.cut != high && n.depth <= HashAnyNode)) {
    if (h.bound == (high ? BOUND_UP : BOUND_LOW) && n.depth > HashWeaken)   // a deep bound against the window
      hash_soften(tt_key(td));
    return NO_VALUE;
  }
  if (h.move && high) {
    if (!n.tact_hash)
      learn_quiet(td, f, h.move, hist_amount(n.expert, n.depth, HashCutQuietMul, 0, HashCutQuietMax),
                  quiet_ctx_slow(td, h.move), n.expert);
    if (n.prev_to >= 0 && f[-1].move_count < HashPrevLate && !n.prior_capture)
      learn_followups(f - 1, td.piece_on[n.prev_to], n.prev_to, -HashCutPrevMalus, n.expert);
  }
  if (td.fifty >= HashFiftyGuard)
    return NO_VALUE;
  if (n.depth >= HashVerify && h.move && td_is_pseudo_legal(td, h.move) && !val_decided(h.value) &&
      !hash_child_agrees(td, n))
    return NO_VALUE;
  return hash_cut_value(n);
}

// Tablebases in interior nodes.
static int tablebase_probe(ThreadData &td, NodeFrame *f, NodeState &n) {
  if (n.root || n.excluded)
    return NO_VALUE;
  unsigned men = syzygy_max_pieces();
  if (g_syzygy_probe_limit >= 0 && (unsigned)g_syzygy_probe_limit < men)
    men = (unsigned)g_syzygy_probe_limit;
  if (!men || td.castle || td.fifty)
    return NO_VALUE;
  const unsigned pieces = (unsigned)count_bits(td.occupancies[both]);
  if (pieces > men || (pieces == men && n.depth < g_syzygy_probe_depth))
    return NO_VALUE;
  int tb;
  if (!syzygy_probe_wdl(td, td.ply, tb))
    return NO_VALUE;
  td.tb_hits++;
  if (td.thread_id == 0)
    g_poll_countdown = 0;
  const int bound = tb > 0 ? BOUND_LOW : tb < 0 ? BOUND_UP : BOUND_EXACT;
  if (bound == BOUND_EXACT)
    tb = contempt(td);   // tablebase draw: the same value as the other draws (0 without Contempt)
  if (bound == BOUND_EXACT || (bound == BOUND_LOW ? tb >= n.beta : tb <= n.alpha)) {
    hash_write(td, tb, f->tt_pv, bound, std::min(max_ply - 1, n.depth + 6), 0, tt_eval_none);
    return tb;
  }
  if (n.pv) {
    if (bound == BOUND_LOW) {
      n.best = tb;
      n.alpha = std::max(n.alpha, n.best);
    } else
      n.cap_value = tb;
  }
  return NO_VALUE;
}

// --------------------------------------------------------------------------
// Fase 2: valutazione e tagli prima del ciclo delle mosse.
// --------------------------------------------------------------------------
static void node_evaluate(ThreadData &td, NodeFrame *f, NodeState &n) {
  n.eval_raw = tt_eval_none;
  if (n.in_check) {
    f->static_eval = n.eval = f[-2].static_eval;
  } else if (n.excluded) {
    n.eval = f->static_eval;
    n.eval_raw = n.h.hit ? n.h.eval_raw : tt_eval_none;
  } else {
    const int plain = node_eval(td, n.h, n.eval_raw);
    f->static_eval = n.eval = corrected_eval(plain, n.signal + n.signal_x);
    if (n.h.hit) {
      if (val_known(n.h.value) && bound_covers(n.h.bound, n.h.value > n.eval))
        n.eval = n.h.value;
    } else
      hash_write(td, VAL_NONE, f->tt_pv, BOUND_NIL, HD_EVAL, 0, n.eval_raw);
  }
  n.improving = f->static_eval > f[-2].static_eval;
  n.opp_worse = f->static_eval > -f[-1].static_eval;
  if (n.prior_red >= 3 && !n.opp_worse)
    n.depth++;
  if (n.prior_red >= 2 && n.depth >= 2 && f->static_eval + f[-1].static_eval > PriorRedEvalSum)
    n.depth--;
}

template <bool AN>
static int prune_before_moves(ThreadData &td, NodeFrame *f, NodeState &n) {
  const int us = td.side;
  // G. The change of the evaluation since the previous ply teaches something about the move that led here.
  if (move_ok(f[-1].move) && !f[-1].in_check && !n.prior_capture) {
    const int diff = std::clamp(-(f[-1].static_eval + f->static_eval), -EvalDiffLo, EvalDiffHi) + EvalDiffOfs;
    const int pm = f[-1].move;
    // Weight per expert (HlEval) and gravity of the expert; EvPen scales the penalties (diff < 0) apart from the
    // rewards (1024 = one weight for both, as before: ev * 1024 / 1024 = ev).
    int ev = by_exp4(n.expert, HlEval0, HlEval1, HlEval2, HlEval3);
    if (diff < 0)
      ev = ev * by_exp4(n.expert, EvPen0, EvPen1, EvPen2, EvPen3) / 1024;
    qh_push(td, us ^ 1, quiet_key(pm), f[-1].hctx, diff * EvalDiffMul * ev / 1024, hist_grav(n.expert));
    const int pc_now = td.piece_on[n.prev_to];
    if (!n.h.hit && pc_now % 6 != P && !get_move_promoted(pm))
      hist_learn(pawn_row(td)[pc_now][n.prev_to], diff * EvalDiffPawnMul * ev / 1024, LIM_PAWN, hist_grav(n.expert));
  }

  // Valutazione molto sotto alpha: basta la quiescenza.
  // gd_razor (AnGold) e' 0 fuori dalla modalita' analisi; n.crystal e' sempre falso.
  if (!n.pv && n.eval < n.alpha - RazorMul * n.depth * n.depth - (AN ? td.gd_razor : 0) && !(AN && n.crystal))
    return quiesce(td, f, n.alpha, n.beta, false);

  // Valutazione abbastanza sopra beta: il nodo regge senza cercare.
  if (!f->tt_pv && n.depth < (n.hunt_mate ? 6 : e_rfp_max_depth(td)) && n.eval >= n.beta && (!n.h.move || n.tact_hash) &&
      !val_lost(n.beta) && !val_won(n.eval) && !(AN && n.crystal)) {
    int mult = std::min(RfpMultBase + n.depth * RfpMultDepth, RfpMultCap) + (AN ? td.dz_rfp : deep_term(td, DeepRfp));
    if (AN && td.gd_rfp_pct != 100)   // AnGold: margine allargato (150% in attacco o con sacrifici, 120% in manovra)
      mult = mult * td.gd_rfp_pct / 100;
    mult -= RfpNoTTHit * !f->tt_hit;
    mult += by_expert(n.expert, RfpExp0, RfpExp1, RfpExp2, RfpExp3);
    const int imp = RfpImproving * n.improving;
    const int margin = mult * n.depth - (imp + RfpOppWorse * n.opp_worse) * mult / 1024 +
                       std::abs(n.signal) / RfpCorrDiv;
    if (n.eval - margin >= n.beta)
      return (RfpBetaWeight * n.beta + (1024 - RfpBetaWeight) * n.eval) / 1024;
  }

  // Mossa nulla: se anche passando la mano si resta sopra beta, il nodo taglia.
  const int non_pawn = td.occupancies[us] & ~td.bitboards[us == white ? P : p] & ~td.bitboards[us == white ? K : k]
                           ? 1 : 0;
  if (n.cut && f->static_eval >= n.beta - NmpDepthMul * n.depth - e_nmp_improving(td) * n.improving + NmpBase &&
      !n.excluded && non_pawn && td.ply >= td.nmp_min_ply && n.beta >= -2000 && !(AN && gd_skip_null(td, n)) &&
      !(AN && (td.an_bits & AN_NMPZUG) && td.root_depth >= 11 && td.side != td.root_side && few_moves(td)) &&
      !(NmpFewMoves && count_bits(td.occupancies[both]) <= NmpFewPieces && few_moves(td, NmpFewMoves))) {
    // DeepNmp in 1024-esimi di ply, arrotondato con 10 bit della chiave (fissi per posizione): a 0 il termine e' 0,
    // altrimenti una frazione |v|/1024 dei nodi riduce di un ply in piu' (o in meno) e lo SPSA ne vede la pendenza.
    const int r = NmpRedBase + n.depth / 3 + std::max((f->static_eval - n.beta) / NmpEvalDiv, 0) +
                  (((AN ? td.dz_nmp : deep_term(td, DeepNmp)) + (int)((td.hash_key >> 20) & 1023)) >> 10);
    NullUndo nu;
    step_in_null(td, f, nu);
    f[1].in_check = false;
    const int nv = -explore<AN>(td, f + 1, -n.beta, -n.beta + 1, n.depth - r, false, false);
    step_out_null(td, nu);
    if (nv >= n.beta && !val_won(nv)) {
      if (td.nmp_min_ply || n.depth < e_nmp_verify_depth(td))
        return nv;
      td.nmp_min_ply = td.ply + 3 * (n.depth - r) / 4;
      const int v = explore<AN>(td, f, n.beta - 1, n.beta, n.depth - r, false, false);
      td.nmp_min_ply = 0;
      if (v >= n.beta)
        return nv;
    }
  }

  n.improving |= f->static_eval >= n.beta;

  // Senza mossa hash, fuori dalla linea precedente, si scende di un ply.
  if (!f->follow_pv && !n.all && n.depth >= 6 && !n.h.move)
    n.depth--;

  // Tagli con le catture: una cattura che supera beta di un margine anche a profondita' ridotta.
  const int cut_beta = n.beta + ProbCutMargin - ProbCutImproving * n.improving;
  if (n.depth >= 3 && !val_decided(n.beta) && !(val_known(n.h.value) && n.h.value < cut_beta)) {
    MoveQueue q;
    queue_start(q, td, f, n.h.move, n.depth, QK_CUTS);
    q.cut_threshold = cut_beta - f->static_eval;
    MoveSafety s;
    s.ready = false;
    const int cut_depth = n.depth - (n.improving ? 5 : 3);
    int m;
    while ((m = queue_next(q)) != 0) {
      const bool gen_legal = queue_move_legal(q, m);
      if (m == n.excluded || (!gen_legal && !legal_before(td, s, m)))
        continue;
      const bool gives = checks_before(td, s, m);
      UndoInfo u;
      if (!step_in(td, f, m, u, gen_legal))
        continue;
      f[1].in_check = gives;
      int v = -quiesce(td, f + 1, -cut_beta, -cut_beta + 1, false);
      if (v >= cut_beta && cut_depth > 0)
        v = -explore<AN>(td, f + 1, -cut_beta, -cut_beta + 1, cut_depth, !n.cut, false);
      step_out(td, m, u);
      if (v >= cut_beta) {
        hash_write(td, v, f->tt_pv, BOUND_LOW, cut_depth + 1, m, n.eval_raw);
        if (!val_decided(v))
          return v - (cut_beta - n.beta);
      }
    }
  }
  return NO_VALUE;
}

// --------------------------------------------------------------------------
// Fase 3: il ciclo delle mosse.
// --------------------------------------------------------------------------

// Potatura di una mossa prima di giocarla. true = salta la mossa.
template <bool AN>
static bool skip_move(ThreadData &td, NodeFrame *f, NodeState &n, MoveQueue &q, int m, bool tact, bool gives,
                      int r, int ctx) {
  const int us = td.side;
  if (n.root || val_lost(n.best) || (AN && n.crystal))   // n.crystal: solo AnCrystal
    return false;
  const U64 own_pieces = td.occupancies[us] & ~td.bitboards[us == white ? P : p] & ~td.bitboards[us == white ? K : k];
  if (!own_pieces)
    return false;
  // (3 + d^2) / (2 - improving) diviso 1 o 2: uno shift, non una divisione hardware (04/10/2026, stesso valore).
  if (f->move_count >= (3 + n.depth * n.depth) >> (1 - (int)n.improving) &&
      !(AN && (td.an_bits & AN_LMPNEAR) && (n.pv || f->ply <= 2)))   // AnLmpNear
    q.quiets_off = true;
  int red_depth = n.depth - 1 - r / 1024;
  const int pc = move_piece(m), to = move_to(m);

  if (tact || gives) {
    const int vk = victim_kind(td, m);
    const int ch = td.capt_hist[pc][to][vk];
    if (!gives && red_depth < 8) {
      const int fut = f->static_eval + CaptFutBase + CaptFutDepth * red_depth + unit_value[vk] + CaptFutHist * ch / 1024;
      if (fut <= n.alpha)
        return true;
    }
    const int margin = CaptSeeDepth * n.depth + ch * CaptSeeHist / 1024;
    // Il sacrificio dell'ultimo pezzo con alpha sotto la patta non si pota (puo' portare allo stallo).
    const int own_count = count_bits(own_pieces);
    const bool last_piece = own_count == 1 && (own_pieces >> move_from(m) & 1);
    if ((n.alpha >= contempt(td) || !last_piece) && !see_holds_q(td, q, m, -margin))
      return true;
    return false;
  }

  if (f->follow_pv && n.pv)
    return false;
  const int d_index = std::min(n.depth, 16) - 1;
  // hist / hist_div[d] con divisore variabile era una divisione hardware a ogni quieta (04/10/2026). Ora moltiplica
  // per il reciproco M = ceil(2^40 / D) e tronca verso zero: per D <= 3637 e' esatto fino a |hist| < 2^27 (errore
  // M*D - 2^40 < D), e |hist| qui sta sotto 2^19 (somme di statistiche limitate). Stesso valore, stesso albero.
  // Divisori = HistDiv1..16 (parametri dal 12/10/2026 sera), tabelle hist_div/hist_recip in testa al file.
  int hist = (*f[-1].cont_hist)[pc][to] + (*f[-2].cont_hist)[pc][to] + pawn_row(td)[pc][to];
  if (hist < -QuietHistPrune * n.depth)
    return true;
  hist += QuietMainHistMul * qh_main(td, us, quiet_key(m), ctx) / 32;
  assert(std::abs(hist) < (1 << 27));
  {
    const unsigned long long mag = (unsigned long long)(hist < 0 ? -hist : hist);
    int q = (int)((mag * hist_recip[d_index]) >> 40);
    if (DeepHist)
      q = q * (1024 + deep_term(td, DeepHist)) / 1024;
    red_depth += hist < 0 ? -q : q;
  }
  const int fut = f->static_eval + QuietFutDepth * red_depth + e_quiet_fut_alpha(td) * (f->static_eval > n.alpha) + QuietFutBase +
                  (AN ? td.dz_fut : deep_term(td, DeepFut)) + by_expert(n.expert, FutExp0, FutExp1, FutExp2, FutExp3);
  if (!n.in_check && red_depth < 12 && fut <= n.alpha) {
    if (n.best <= fut && !val_decided(n.best) && !val_won(fut))
      n.best = fut;
    return true;
  }
  red_depth = std::max(red_depth, 0);
  return !see_holds_q(td, q, m, -QuietSeeMul * red_depth * red_depth);
}

// Spinta di un pedone passato in 6a o 7a traversa in un finale (nostra). La rete vede i pedoni passati con il suo
// blocco PassedPawns; la ricerca li tratterebbe come quiete qualsiasi e potrebbe ridurli di parecchi ply.
static bool passed_push(const ThreadData &td, int m) {
  if (kPieceKind[move_piece(m)] != P || get_move_capture(m) || get_move_promoted(m))
    return false;
  const int to = move_to(m), us = td.side, row = to / 8, file = to % 8;   // row 0 = ottava traversa
  if (us == white ? (row != 1 && row != 2) : (row != 5 && row != 6))
    return false;
  int npm = 0;
  for (int t = N; t <= Q; t++)
    npm += unit_value[t] * (count_bits(td.bitboards[t]) + count_bits(td.bitboards[t + 6]));
  if (npm > PassedPushNpm)
    return false;
  U64 files = 0;
  for (int fl = std::max(0, file - 1); fl <= std::min(7, file + 1); fl++)
    files |= 0x0101010101010101ULL << fl;
  const int row_start = to - file;
  const U64 ahead = us == white ? (1ULL << row_start) - 1 : ~((1ULL << (row_start + 8)) - 1);
  return !(files & ahead & td.bitboards[us == white ? p : P]);
}

// La mossa e' uno spostamento avanti e indietro senza progresso (stessa pedina fra le stesse case)?
static inline bool shuffles(const ThreadData &td, const NodeFrame *f, int m) {
  if (is_tactical(m) || td.fifty < 10 || td.plies_from_null < 6 || f->ply < 20)
    return false;
  const int m2 = f[-2].move, m4 = f[-4].move;
  return move_ok(m2) && move_ok(m4) && move_from(m) == move_to(m2) && move_from(m2) == move_to(m4);
}

// Estensione singolare della mossa hash. Ritorna l'estensione; un valore >= beta chiude il nodo (in *cutoff).
// SING_SKIPPED: la ricerca di esclusione non si e' fatta (mossa non hash o condizioni mancanti); move_loop la legge
// come estensione 0, e solo in quel caso prova la LDSE (Ldse*).
static constexpr int SING_SKIPPED = -99;
template <bool AN>
static int singular_probe(ThreadData &td, NodeFrame *f, NodeState &n, int m, int new_depth, int *cutoff) {
  const HashView &h = n.h;
  if (n.root || m != h.move || n.excluded || n.depth < 6 + f->tt_pv || !val_known(h.value) || val_decided(h.value) ||
      !bound_covers(h.bound, true) || h.bound == BOUND_UP || h.depth < n.depth - 3 || shuffles(td, f, m) || n.hunt_mate)
    return SING_SKIPPED;
  const int s_beta = h.value - (SingMarginBase + (AN ? td.dz_sing : deep_term(td, DeepSing)) + SingMarginTtPv * (f->tt_pv && !n.pv)) * n.depth / 63;
  const int s_depth = new_depth / 2;
  f->excluded = m;
  const int v = explore<AN>(td, f, s_beta - 1, s_beta, s_depth, n.cut, false);
  f->excluded = 0;
  if (v < s_beta) {
    const int corr_adj = std::abs(n.signal) / SingCorrDiv;
    const int dbl = SingDoubleBase + SingDoublePv * n.pv - SingDoubleQuiet * !n.tact_hash - corr_adj -
                    SingTtHistMul * td.tt_move_hist / SingTtHistDiv - (f->ply > td.root_depth) * SingDoubleDeep -
                    (AN ? td.dz_ext : deep_term(td, DeepExt));
    const int tpl = SingTripleBase + SingTriplePv * n.pv - SingTripleQuiet * !n.tact_hash + SingTripleTtPv * f->tt_pv -
                    corr_adj - (f->ply > td.root_depth) * SingTripleDeep - (AN ? td.dz_ext : deep_term(td, DeepExt));
    n.depth++;
    return 1 + (v < s_beta - dbl) + (v < s_beta - tpl);
  }
  if (v >= n.beta && !val_decided(v)) {
    hist_learn(td.tt_move_hist, -SingFhTtBase - SingFhTtDepth * n.depth, LIM_TTMOVE, ttm_grav(n.expert));
    if (!n.in_check && v > f->static_eval) {
      const int bonus = std::clamp((v - f->static_eval) * s_depth * CorrSingMul / 1024, -LIM_CORR / 4, LIM_CORR / 4);
      corr_learn(td, f, bonus, n.expert);
    }
    *cutoff = v;
    return 0;
  }
  if (h.value >= n.beta || n.cut)
    return -3;
  return 0;
}

// Aggiorna la mossa di radice appena cercata (statistiche, linea, cambi di mossa migliore).
static void root_record(ThreadData &td, NodeState &n, int m, int value, U64 spent, int move_no) {
  RootLine *rl = nullptr;
  for (int i = 0; i < td.root_count; i++)
    if (td.root[i].pv[0] == m) { rl = &td.root[i]; break; }
  if (!rl)
    return;
  rl->effort += spent;
  const U64 prev = std::max<U64>(1, rl->effort - spent);
  const U64 w = std::clamp<U64>((32 * spent * 2) / (spent * 2 + 3 * prev), 12, 24);
  const U64 w_sq = std::min<U64>(w, 16);
  const long long v2 = (long long)value * std::abs(value);
  rl->avg_score = rl->avg_score == -infinity ? value : (int)((value * (long long)w + rl->avg_score * (long long)(32 - w)) / 32);
  rl->mean_sq = rl->mean_sq == -(long long)infinity * infinity ? v2
              : (v2 * (long long)w_sq + rl->mean_sq * (long long)(32 - w_sq)) / 32;
  if (move_no == 1 || value > n.alpha) {
    rl->score = rl->uci_score = value;
    rl->sel_depth = td.sel_depth;
    rl->lower = rl->upper = false;
    if (value >= n.beta) { rl->lower = true; rl->uci_score = n.beta; }
    else if (value <= n.alpha) { rl->upper = true; rl->uci_score = n.alpha; }
    rl->pv_len = 1;
    for (int i = 1; i < td.pv_length[1] && rl->pv_len < max_ply; i++)
      rl->pv[rl->pv_len++] = td.pv_table[1][i];
    if (move_no > 1 && !td.pv_idx)
      td.best_move_changes++;
  } else
    rl->score = -infinity;
}

template <bool AN>
static int move_loop(ThreadData &td, NodeFrame *f, NodeState &n) {
  const int us = td.side;
  MoveQueue q;
  queue_start(q, td, f, n.h.move, n.depth, n.in_check ? QK_EVADE : QK_MAIN);
  MoveSafety s;
  s.ready = false;
  q.checks = &s.checks;
  int value = n.best, m;
  int red_window = -1, red_window_term = 0, red_window_ours = 0;   // vedi base_reduction; ours: per 1024 of root_delta
  int first_quiet_no = 0;                     // LmrRank: numero della prima quieta ordinata del nodo
  // Idea 2: termine di riduzione fisso per il nodo (0 se spento).
  const int node_red = AN ? td.dz_lmr : deep_term(td, DeepLmr);
  const int gd_style = AN ? td.gd_style : 0;   // AnGold: stile della radice (0 in partita)

  while ((m = queue_next(q)) != 0) {
    const bool gen_legal = queue_move_legal(q, m);
    if (m == n.excluded || (!gen_legal && !legal_before(td, s, m)))
      continue;
    int root_rank = 0;   // posizione della mossa nella lista della radice (ordine dell'iterazione precedente)
    if (n.root) {
      bool listed = false;
      for (int i = td.pv_idx; i < td.pv_last; i++)
        if (td.root[i].pv[0] == m) { listed = true; root_rank = i - td.pv_idx; break; }
      if (!listed)
        continue;
    }
    f->move_count = ++n.moves;
    // "info currmove" alla radice (come la 7.0 e gli SF vecchi: solo thread principale, solo dopo 3 s; SF19 usa invece
    // 10 milioni di nodi del thread principale, NODES_LIMIT_OUTPUT, corretto il 16/10/2026): le GUI mostrano la
    // mossa in esame. Persa con la riscrittura del 04/10, segnalata da Maurizio Platino il 12/10/2026 (Fritz 18).
    // Il tempo la tiene fuori da bench e partite brevi; a radice il ciclo gira poche volte, costo nullo.
    if (n.root && td.thread_id == 0 && get_time_ms() - g_limits.start_ms > 3000) {
      std::lock_guard<std::mutex> lock(output_mutex);
      printf("info depth %d currmove ", td.root_depth);
      print_move(m);
      printf(" currmovenumber %d\n", n.moves + td.pv_idx);
      fflush(stdout);
    }
    const bool tact = is_tactical(m);
    const int pc = move_piece(m), to = move_to(m);
    const bool gives = checks_before(td, s, m);
    int new_depth = n.depth - 1;
    if (n.beta - n.alpha != red_window) {
      red_window = n.beta - n.alpha;
      red_window_term = red_window * LmrDeltaMul / td.root_delta;
      red_window_ours = red_window * 1024 / td.root_delta;
    }
    // LmrRank (blocco 2 della 8.1, 16/10/2026, nostra; docs/development/8.1/SEARCH_2_REDUCTIONS.md nel repo): per una
    // quieta della lista ordinata, la riduzione di base non legge solo il suo numero d'ordine. Il numero "effettivo"
    // e' il numero della prima quieta del nodo piu' una mossa ogni LmrRankScale punti di distacco dal punteggio della
    // quieta migliore: la decima mossa con un punteggio vicino alla prima non e' la decima di una coda di mosse
    // cattive, e nella coda non ordinata (sotto il limite di fill_quiets) il distacco vale piu' dell'ordine di
    // generazione. I due numeri si mescolano con un peso per esperto (LmrRankMix0-3). Spenta: n.moves, come prima.
    int red_move_no = n.moves;
    if (LmrRank && q.rank_ok) {
      if (!first_quiet_no)
        first_quiet_no = n.moves;
      const int eff = first_quiet_no + std::max(0, q.quiet_top - q.last_score) / LmrRankScale;
      const int mix = by_expert(n.expert, LmrRankMix0, LmrRankMix1, LmrRankMix2, LmrRankMix3);
      red_move_no = std::clamp((n.moves * (1024 - mix) + eff * mix) / 1024, 1, 255);
    }
    int r;
    if (RedForm) {   // block 2: our reduction, the value the pruning rules read
      r = red_entry_ours(f, n, red_move_no, red_window_ours);
      if (gd_style)
        r = gd_style == 1 ? r * 97 / 100 : gd_style == 2 ? r * 103 / 100 : r;
    } else {
      r = base_reduction(td, n.improving, n.depth, red_move_no, red_window_term);
      if (gd_style)   // AnGold: riduzione di base al 97% in attacco, al 103% in manovra
        r = gd_style == 1 ? r * 97 / 100 : gd_style == 2 ? r * 103 / 100 : r;
      // LmrScaleExp (13/10/2026, spenta): pendenza della riduzione per esperto (LmrExp ne sposta solo il livello).
      if (LmrScaleExp0 | LmrScaleExp1 | LmrScaleExp2 | LmrScaleExp3)
        r = r * (1024 + by_expert(n.expert, LmrScaleExp0, LmrScaleExp1, LmrScaleExp2, LmrScaleExp3)) / 1024;
      if (f->tt_pv)
        r += LmrTtPvAdd;
      r += by_expert(n.expert, LmrExp0, LmrExp1, LmrExp2, LmrExp3);
    }
#ifdef TRIUMV_REDLOG
    const int redlog_entry = r;   // the value skip_move reads
#endif

    const bool passer = PassedPushRed && !tact && passed_push(td, m);
    const int ctx = tact ? 0 : quiet_ctx(q, m);   // ThreatHist: 0 se spenta
    if (!passer && skip_move<AN>(td, f, n, q, m, tact, gives, r, ctx))
      continue;

    int cut_value = NO_VALUE;
    int ext = singular_probe<AN>(td, f, n, m, new_depth, &cut_value);
    if (cut_value != NO_VALUE)
      return cut_value;
    const bool sing_skipped = ext == SING_SKIPPED;
    if (sing_skipped)
      ext = 0;
    // LDSE (nostra, 05/10/2026; l'idea e' in Reckless e Stormphrax): a profondita' bassa, dove la singolare non si
    // prova, la mossa hash con limite inferiore o esatto si estende di un ply se la valutazione statica sta sotto
    // alpha di LdseMargin. La hash dice che quella mossa ha tenuto un valore, l'eval dice che la posizione e' brutta:
    // il nodo si regge su di lei, e vicino all'orizzonte un ply in piu' decide se quel valore era vero.
    // LdseMode (bit): 1 = solo nei nodi cut (forma di Reckless), 2 = solo limite inferiore, non esatto (forma di
    // Stormphrax), 4 = non con entry della sola quiescenza.
    // LdseMax (05/10/2026): con Ldse accesa ogni nodo cut lungo una linea puo' estendere di nuovo, e la profondita'
    // scende di un ply ogni due invece di uno per ply. A profondita' fissa l'albero resta lo stesso nella posizione
    // tipica (media geometrica x1,02-x1,06 su 80 posizioni, prof. 8-14) ma alcuni finali di torre crescono x5-x16. Il
    // tetto conta le estensioni Ldse sulla linea (NodeFrame::ldse_path); 0 = nessun tetto, come prima.
    const bool ldse = LdseMargin && ext == 0 && sing_skipped && m == n.h.move && !n.root && !n.excluded &&
                      !n.in_check && n.depth <= LdseDepth && val_known(n.h.value) && bound_covers(n.h.bound, true) &&
                      f->static_eval <= n.alpha - LdseMargin && (n.cut || !(LdseMode & 1)) &&
                      (n.h.bound == BOUND_LOW || !(LdseMode & 2)) && (n.h.depth >= 0 || !(LdseMode & 4)) &&
                      (!LdseMax || f->ldse_path < LdseMax);
    if (ldse)
      ext = 1;
    f->ldse = ldse;

    const U64 nodes_before = td.nodes;
    UndoInfo u;
    if (!step_in(td, f, m, u, gen_legal)) {
      f->move_count = --n.moves;
      continue;
    }
    f[1].in_check = gives;
    f->hctx = (unsigned char)ctx;
    if (CausalRed)
      f[1].refute = 0;
    new_depth += ext;

    if (tact) {
      // Pezzo davvero catturato (per l'en passant e' il pedone, non la casa d'arrivo vuota).
      const int took = kPieceKind[u.captured_piece & 15];
      f->stat_score = CaptStatMul * unit_value[took] / 128 + td.capt_hist[pc][to][took];
    }
    else
      f->stat_score = (StatQuietW * qh_main(td, us, quiet_key(m), ctx) + StatCont1W * (*f[-1].cont_hist)[pc][to] +
                       StatCont2W * (*f[-2].cont_hist)[pc][to]) / 1024;
    if (RedForm) {   // block 2: our reduction for the search of this move
      const bool causal_hit = CausalRed && n.causal && !(((1ULL << move_from(m)) | (1ULL << to)) & n.causal);
      r = red_final_ours(f, n, red_move_no, tact, gives, passer, causal_hit, red_window_ours) + node_red;
    } else {
    if (f->tt_pv)
      r -= LmrTtPvSub + deep_term(td, DeepTtPv) + n.pv * LmrTtPvPv + (n.h.value > n.alpha) * LmrTtPvAlpha +
           (n.h.depth >= n.depth) * (LmrTtPvDepth + n.cut * LmrTtPvCut);
    r += e_lmr_offset(td);
    r -= n.moves * LmrMoveCount;
    r -= std::abs(n.signal) / LmrCorrDiv;
    if (n.cut)
      r += LmrCutNode + LmrCutNoTT * !n.h.move;
    if (n.tact_hash)
      r += LmrTTCapture;
    r += node_red;
    if (f[1].cutoff_cnt > 1)
      r += LmrCutoffBase + LmrCutoffMany * (f[1].cutoff_cnt > 2) + LmrCutoffAll * n.all;
    else if (m == n.h.move)
      r -= LmrTTMove;
    r -= f->stat_score * LmrStatDiv / 4096;
    if (!tact && !val_decided(n.alpha))
      r += 3 * std::clamp(n.alpha - n.eval, -64, 96);
    if (n.all) {
      // r * LmrAllNode / (256*depth + LmrAllDivBase), troncato verso zero come la divisione intera: moltiplicazione per il
      // reciproco a 40 bit della tabella (esatta finche' |r*LmrAllNode| < 2^24: il divisore arriva a 35.000, quindi
      // l'errore M*D - 2^40 < D non puo' far scattare l'unita'); oltre, mai visto, si divide davvero
      // (04/10/2026 sera: era una divisione hardware per ogni mossa dei nodi all).
      const long long x = (long long)r * e_lmr_all_node(td);
      const unsigned long long mag = (unsigned long long)(x < 0 ? -x : x);
      const int q = mag < (1ULL << 24) ? (int)((mag * all_node_recip[n.depth]) >> 40)
                                       : (int)(mag / (unsigned long long)(256 * n.depth + LmrAllDivBase));
      r += x < 0 ? -q : q;
    }
    if (passer)
      r -= PassedPushRed;
    // CausalRed: la quieta non tocca le case della confutazione di una sorella fallita bassa.
    if (CausalRed && n.causal && !tact && !gives && !passer && m != n.h.move &&
        !(((1ULL << move_from(m)) | (1ULL << to)) & n.causal))
      r += CausalRed;
    }
    // AnGold, ritocchi delle riduzioni lontano dalla radice (unita' 1/1024 di ply): in attacco catture e scacchi
    // -60, in difesa le quiete -40, in manovra le quiete senza scacco +80 da profondita' 9.
    if (gd_style && n.depth > 5 && f->ply > 1) {
      if (gd_style == 1 && (tact || gives))
        r -= 60;
      else if (gd_style == 3 && !tact)
        r -= 40;
      else if (gd_style == 2 && !tact && !gives && n.depth > 8)
        r += 80;
    }

#ifdef TRIUMV_REDLOG
    // Block 2 data (18/10/2026): the features of a sampled reduced move, the reduction it gets and, written after its
    // searches, what happened (redlog.h, version 2). Some moves that fail low at the reduced depth are searched again
    // at full depth only to label them (probe): that extra search changes the tree of the data build only.
    const bool redlog_on = !AN && n.depth >= 2 && n.moves > 1 && redlog_pick(td.thread_id);
    const int redlog_flags =
        (n.improving << 0) | (f->tt_pv << 1) | (n.pv << 2) | (n.cut << 3) | (n.all << 4) | (tact << 5) |
        (gives << 6) | ((m == n.h.move) << 7) | (n.tact_hash << 8) | ((n.h.value > n.alpha) << 9) |
        ((n.h.depth >= n.depth) << 10) | (passer << 11) |
        ((CausalRed && n.causal && !(((1ULL << move_from(m)) | (1ULL << to)) & n.causal)) << 12) |
        ((!n.h.move) << 13) | (1 << 14) | (n.opp_worse << 15);
    const int redlog_gap = val_decided(n.alpha) ? 0 : n.alpha - n.eval, redlog_r = r, redlog_cuts = f[1].cutoff_cnt;
    const int redlog_alpha = n.alpha;
#endif
    if (n.depth >= 2 && n.moves > 1) {
      // NegExtMax (08/10/2026): con riduzione negativa la mossa si cerca fino a 2 ply PIU' a fondo, e lungo una linea
      // queste estensioni si sommano come quelle di Ldse prima della sua guardia (LdseMax). Stesso rimedio: un tetto al
      // numero di estensioni da riduzione negativa sulla linea (NodeFrame::negext_path).
      // BAKE NEGEXTMAX (11/10/2026, vedi 01_params.inc): tetto fisso a una estensione per linea, opzione ritirata.
      const int neg_cap = f->negext_path < 1 ? 2 : 0;
      if (AN && n.no_lmr && r > 0)   // AnKingDanger / AnCycle: niente riduzione, le riduzioni negative restano
        r = 0;
      if (AN && n.root && (td.an_bits & AN_ROOTALT) && root_rank >= 1 && root_rank <= 2 && r > 0)   // AnRootAlt
        r = 0;
      if (AN && tact && (td.an_bits & AN_CAPTLMR) && !(n.cut && f[-1].move_count > 1) && r > 0)    // AnCaptLmr
        r = 0;
      const int d = std::max(1, std::min(new_depth - r / 1024, new_depth + neg_cap)) + n.pv;
      f->negext = d - n.pv > new_depth;
      f->reduction = new_depth - d;
      value = -explore<AN>(td, f + 1, -(n.alpha + 1), -n.alpha, d, true, false);
      f->reduction = 0;
      f->negext = 0;
#ifdef TRIUMV_REDLOG
      const int value_red = value;
#endif
      if (value > n.alpha) {
#ifdef TRIUMV_REDLOG
        if (redlog_on && d < new_depth)
          redres_probes<AN>(td, f, n, 1, d, new_depth, new_depth - 1, value, red_move_no, redlog_r, redlog_flags);
#endif
        if (ResOwn) {   // ours (01_params.inc, off): the margin over alpha must grow with the reduction and the correction
          const int u = std::max(ResU0 + ResUc * (std::abs(n.signal) >> 16) / 64 + ResUr * (new_depth - d), 1);
          const int margin = (value - n.alpha) * 64;
          new_depth += (d < new_depth && margin > ResKp * u) - (margin < ResKm * u);
        } else
          new_depth += (d < new_depth && value > n.best + DeeperMargin) - (value < n.best + ShallowerMargin);
        if (new_depth > d)
          value = -explore<AN>(td, f + 1, -(n.alpha + 1), -n.alpha, new_depth, !n.cut, false);
        learn_followups(f, pc, to, FollowFhBonus, n.expert);
      }
#ifdef TRIUMV_REDLOG
      if (redlog_on && !stop_threads.load(std::memory_order_relaxed)) {
        // outcome bits: 1 reduced search above alpha, 2 re-searched, 4 final value above alpha, 8 probed, 16 probe
        // above alpha (full-depth null-window search of a move that failed low reduced)
        const int full = n.depth - 1 + ext;
        int outcome = (value_red > redlog_alpha) | ((new_depth > d && value_red > redlog_alpha) << 1) |
                      ((value > redlog_alpha) << 2);
        if (value_red <= redlog_alpha && d < full && redlog_probe(td.thread_id)) {
          const int vp = -explore<AN>(td, f + 1, -(redlog_alpha + 1), -redlog_alpha, full, !n.cut, false);
          if (!stop_threads.load(std::memory_order_relaxed))
            outcome |= 8 | ((vp > redlog_alpha) << 4);
        }
        redlog_write(td.thread_id, n.depth, n.moves, red_move_no, n.expert, f->stat_score, redlog_gap, redlog_cuts,
                     redlog_entry, redlog_r, n.signal, red_window_term, f->ply, redlog_flags, outcome, d, full,
                     std::clamp(n.h.value - f->static_eval, -32000, 32000), val_known(n.h.value));
      }
#endif
    } else if (!n.pv || n.moves > 1) {
      if (!n.h.move)
        r += e_lmr_no_tt_full(td);
#ifdef TRIUMV_REDLOG
      if (!AN && !n.pv && n.moves == 1 && n.depth >= 2 && new_depth >= 3 && redres_pick(td.thread_id))
        redres_probes<AN>(td, f, n, 2, new_depth, new_depth, new_depth - 2, 0, red_move_no, r, redlog_flags);
#endif
      value = -explore<AN>(td, f + 1, -(n.alpha + 1), -n.alpha,
                       new_depth - (r > LmrDeepRed1) - (r > LmrDeepRed2 && new_depth > 2), !n.cut, false);
    }
    if (n.pv && (n.moves == 1 || value > n.alpha)) {
      if (m == n.h.move && ((val_known(n.h.value) && val_decided(n.h.value) && n.h.depth > 0) || n.h.depth > 1))
        new_depth = std::max(new_depth, 1);
      value = -explore<AN>(td, f + 1, -n.beta, -n.alpha, new_depth, false, true);
    }
    step_out(td, m, u);

    if (stop_threads.load(std::memory_order_relaxed))
      return 0;
    if (CausalRed && !tact && value <= n.alpha && f[1].refute)
      n.causal = causal_mask(td, f[1].refute);
    if (n.root)
      root_record(td, n, m, value, td.nodes - nodes_before, n.moves);

    const int nudge = (value == n.best && td.ply + 2 >= td.root_depth && (int(td.nodes) & 14) == 0 &&
                       !val_won(std::abs(value) + 1)) ? 1 : 0;
    if (value + nudge > n.best) {
      n.best = value;
      if (value + nudge > n.alpha) {
        n.best_move = m;
        n.best_ctx = ctx;
        if (n.pv)
          pv_extend(td, m);
        if (value >= n.beta) {
          f->cutoff_cnt += (ext < 2) || n.pv;
          if (CausalRed)
            f->refute = m;   // per il padre: la mossa che confuta la sua
          break;
        }
        if (n.depth > 3 && n.depth < 12 && !val_decided(value) &&
            !(AN && (td.an_bits & AN_KEEPDEPTH) && f->ply <= kAnKeepPly))   // AnKeepDepth: vicino alla radice no
          n.depth -= 3;
        n.alpha = value;
      }
    }
    if (m != n.best_move && n.moves <= TRACKED) {
      if (tact) n.tacts[n.n_tacts++] = m;
      else {
        n.quiet_ctx[n.n_quiets] = (unsigned char)ctx;
        n.quiets[n.n_quiets++] = m;
      }
    }
  }
  return NO_VALUE;
}

// --------------------------------------------------------------------------
// Fase 4: fine del nodo.
// --------------------------------------------------------------------------
static int node_close(ThreadData &td, NodeFrame *f, NodeState &n) {
  const int us = td.side;
  // La linea del bucket hash di questo nodo, letta all'ingresso, dopo il sottoalbero e' spesso uscita dalla cache:
  // la si richiama adesso, con le statistiche da aggiornare a coprire l'attesa, prima della hash_write in fondo.
  TT_PREFETCH(&hash_table[tt_base_index(tt_key(td))]);
  if (n.best >= n.beta && !val_decided(n.best) && !val_decided(n.alpha)) {
    // W1 (08/10/2026): divisione per depth + 1 col reciproco a 40 bit; |x| < 2^15 * (max_ply + 8) < 2^24, esatto
    // (numpy: 0 differenze per |x| <= 32768 * (d + 1), d < 260).
    const long long x = (long long)n.best * n.depth + n.beta;
    const unsigned long long mag = (unsigned long long)(x < 0 ? -x : x);
    const int q = (int)((mag * succ_recip[n.depth]) >> 40);
    n.best = x < 0 ? -q : q;
  }

  if (!n.moves)
    n.best = n.excluded ? n.alpha : n.in_check ? mated_at(td.ply) : contempt(td);   // stallo: patta (Contempt)
  else if (n.best_move) {
    reward_best(td, f, n);
    if (!n.pv)
      hist_learn(td.tt_move_hist, n.best_move == n.h.move ? TtMoveHistGood : -TtMoveHistBad, LIM_TTMOVE,
                 ttm_grav(n.expert));
  } else if (!n.prior_capture && n.prev_to >= 0) {
    // H. No move raised alpha: the opponent's move that led here was good. Per expert: the move number from which the
    //    previous move counts as late (PrLate, was 9) and a gain on the scale (PrGain, /1024). Defaults give the old
    //    tree (scale * 1024 / 1024 = scale).
    const int late_n = by_exp4(n.expert, PrLate0, PrLate1, PrLate2, PrLate3);
    int scale = -PriorScaleBase - f[-1].stat_score / PriorScaleStatDiv +
                std::min(PriorScaleDepthMul * n.depth, PriorScaleDepthMax) + PriorScaleLate * (f[-1].move_count > late_n) +
                PriorScaleEval * (!n.in_check && n.best <= f->static_eval - PriorScaleEvalMargin) +
                PriorScaleOppEval * (!f[-1].in_check && n.best <= -f[-1].static_eval - PriorScaleOppMargin);
    scale = std::max(scale, 0) * by_exp4(n.expert, PrGain0, PrGain1, PrGain2, PrGain3) / 1024;
    const int bonus = hist_amount(n.expert, n.depth, PriorBonusDepthMul, PriorBonusSub, PriorBonusMax) * scale;
    const int prev_pc = td.piece_on[n.prev_to];
    learn_followups(f - 1, prev_pc, n.prev_to, bonus * PriorContW / 16384, n.expert);
    qh_push(td, us ^ 1, quiet_key(f[-1].move), f[-1].hctx, bonus * PriorQuietW / 32768, hist_grav(n.expert));
    if (prev_pc % 6 != P && !get_move_promoted(f[-1].move))
      hist_learn(pawn_row(td)[prev_pc][n.prev_to], bonus * PriorPawnW / 8192, LIM_PAWN, hist_grav(n.expert));
  } else if (n.prior_capture && n.prev_to >= 0) {
    hist_learn(td.capt_hist[td.piece_on[n.prev_to]][n.prev_to][f[-1].captured % 6],
               by_exp4(n.expert, PrCapt0, PrCapt1, PrCapt2, PrCapt3), LIM_CAPT, hist_cgrav(n.expert));
  }

  if (n.pv)
    n.best = std::min(n.best, n.cap_value);
  if (n.best <= n.alpha)
    f->tt_pv = f->tt_pv || f[-1].tt_pv;

  if (!n.excluded && !(n.root && td.pv_idx))
    hash_write(td, n.best, f->tt_pv,
               n.best >= n.beta ? BOUND_LOW : (n.pv && n.best_move) ? BOUND_EXACT : BOUND_UP,
               n.moves ? n.depth : std::min(max_ply - 1, n.depth + 6), n.best_move, n.eval_raw);

  // CorrLearnMode (06/10/2026, rapporto N_CORREZIONI 4.5): a 0 la condizione di sempre.
  const bool learn_ok = !((CorrLearnMode & 1) && n.excluded && !n.moves) && !((CorrLearnMode & 2) && n.excluded) &&
                        !((CorrLearnMode & 4) && val_decided(n.best));
  const bool dir_ok = (n.best > f->static_eval) == (n.best_move != 0) ||
                      ((CorrLearnMode & 8) && n.pv && n.best_move && n.best < n.beta);
#ifdef TRIUMV_CORRSTATS
  // Residuo prima e dopo la correzione (cstats.h), PRIMA del filtro di direzione (che sceglierebbe il segno):
  // nodi fuori scacco, non esclusi, senza cattura migliore, con un'eval della rete nota.
  if (!n.in_check && !n.excluded && !(n.best_move && get_move_capture(n.best_move)) && n.eval_raw != tt_eval_none &&
      !val_decided(n.best)) {
    const int plain = eval_refade(n.eval_raw, td.fifty);
    const int fb = td.fifty < 20 ? 0 : td.fifty < 60 ? 1 : 2;
    const int kind = n.best_move ? (n.best >= n.beta ? 0 : 1) : 2;   // fail-high, esatto, fail-low
    CSTAT(net_phase(count_bits(td.occupancies[both])), fb, kind, n.best - plain, n.best - f->static_eval);
  }
#endif
  if (!n.in_check && !(n.best_move && get_move_capture(n.best_move)) && dir_ok && learn_ok) {
    // I. Reward of the correction tables: depth enters as d + q*d*d/64, q per expert (CorrDepthQ; 0 = the old linear
    //    form: x * 64d * k / (128 * 64) truncates like x * d * k / 128).
    const long long dq = 64LL * n.depth + (long long)by_exp4(n.expert, CorrDepthQ0, CorrDepthQ1, CorrDepthQ2, CorrDepthQ3) * n.depth * n.depth;
    const int bonus = (int)std::clamp((long long)(n.best - f->static_eval) * dq * (n.best_move ? CorrBonusHi : CorrBonusLo) / 8192,
                                      (long long)-CorrBonusMaxLo, (long long)CorrBonusMaxHi);
    corr_learn(td, f, CorrLearnW * bonus / 1024, n.expert);
  }
  return n.best;
}

// --------------------------------------------------------------------------
template <bool AN>
static int explore(ThreadData &td, NodeFrame *f, int alpha, int beta, int depth, bool cut, bool pv) {
  if (depth <= 0)
    return ((AN && (td.an_bits & AN_QSCHK)) || (QsChecksPieces && count_bits(td.occupancies[both]) <= QsChecksPieces))
               ? quiesce<true>(td, f, alpha, beta, pv)
               : quiesce<false>(td, f, alpha, beta, pv);
  td.pv_length[td.ply] = td.ply;
  NodeState n;
  n.root = f == &td.frames[FRAME_OFFSET];
  n.pv = pv || n.root;
  n.cut = cut;
  n.all = !(n.pv || cut);
  n.alpha = alpha;
  n.beta = beta;
  n.depth = std::min(depth, max_ply - 1);
  n.in_check = f->in_check;
  n.prior_capture = f[-1].captured >= 0;
  n.hunt_mate = td.root_depth >= (AN ? td.hunt_from : 16) && std::abs(td.root[td.pv_idx].score) >= 2000;
  n.best = -infinity;
  n.best_move = 0;
  n.best_ctx = 0;
  n.moves = 0;
  n.cap_value = infinity;
  n.n_quiets = n.n_tacts = 0;
  n.causal = 0;
  // LdseMax: estensioni Ldse sulla linea fino a qui. f->ldse torna a 0 finche' move_loop non estende una mossa, cosi'
  // i figli della mossa nulla e del probcut non ne ereditano una.
  f->ldse = 0;
  f->ldse_path = f[-1].ldse_path + f[-1].ldse;
  // Stesso conteggio per le estensioni da riduzione negativa (move_loop; BAKE NEGEXTMAX 11/10/2026).
  f->negext = 0;
  f->negext_path = f[-1].negext_path + f[-1].negext;

  int v = node_open(td, f, n);
  if (v != NO_VALUE) return v;
  n.crystal = AN && crystal_node(td, f);   // dopo node_open, che decide follow_pv
  // AnKingDanger / AnCycle (Pos Solver, 14/10/2026): in partita an_bits = 0 e i due flag restano falsi.
  n.no_early = n.no_lmr = false;
  if (AN && (td.an_bits & (AN_KDANGER | AN_CYCLE)) && !n.root) {
    if ((td.an_bits & AN_CYCLE) && td_upcoming_repetition(td))
      n.no_early = n.no_lmr = true;
    if (td.an_bits & AN_KDANGER) {
      if (!n.no_early && king_in_danger(td, td.side))
        n.no_early = true;
      if (!n.no_lmr && f->ply <= 6 && king_in_danger(td, td.side ^ 1))
        n.no_lmr = true;
    }
  }
  node_evaluate(td, f, n);
  if ((v = hash_cutoff(td, f, n)) != NO_VALUE) return v;
  if ((v = tablebase_probe(td, f, n)) != NO_VALUE) return v;
  if (!n.in_check && !n.no_early) {
    if ((v = prune_before_moves<AN>(td, f, n)) != NO_VALUE) return v;
  }

  // Limite inferiore dalla hash ben sopra beta: basta quello.
  const int hash_beta = n.beta + ProbCutTTMargin;
  if (bound_covers(n.h.bound, true) && n.h.depth >= n.depth - 4 && n.h.value >= hash_beta && !val_decided(n.beta) &&
      val_known(n.h.value) && !val_decided(n.h.value))
    return hash_beta;

  if ((v = move_loop<AN>(td, f, n)) != NO_VALUE)
    return v;
  return node_close(td, f, n);
}
