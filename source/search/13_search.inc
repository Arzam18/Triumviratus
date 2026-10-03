// search/13_search.inc -- Ricerca principale.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.
//
// Un nodo passa per quattro fasi, ognuna una funzione: ingresso (patte, matti, hash, tablebase), valutazione e
// tagli prima delle mosse, ciclo delle mosse, apprendimento a fine nodo. Lo stato del nodo vive in NodeState.

static int explore(ThreadData &td, NodeFrame *f, int alpha, int beta, int depth, bool cut, bool pv);

constexpr int TRACKED = 32;   // mosse provate ricordate per i malus

struct NodeState {
  bool pv, root, cut, all;
  bool in_check, improving, opp_worse, tact_hash, prior_capture, hunt_mate;
  int  alpha, beta, depth;
  int  best, best_move, cap_value;
  int  moves;                     // mosse legali cercate (il contatore del frame lo riusa la ricerca singolare)
  int  eval, eval_raw, signal;
  int  prev_to;
  int  excluded;
  int  prior_red;
  HashView h;
  int  quiets[TRACKED], n_quiets;
  int  tacts[TRACKED], n_tacts;
};

static constexpr int NO_VALUE = 1 << 30;   // "nessun valore": la fase non ha chiuso il nodo

// Profondita' ridotta di base: tabella logaritmica, finestra relativa e assenza di miglioramento.
static inline int base_reduction(const ThreadData &td, bool improving, int depth, int move_no, int window) {
  const int scale = red_log[std::min(depth, 255)] * red_log[std::min(move_no, 255)];
  return scale - window * LmrDeltaMul / td.root_delta + (!improving ? scale * LmrNotImproving / 512 : 0) + LmrBase;
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
  const int bm = n.best_move;
  int bonus = std::min(BonusMul * n.depth - BonusSub, BonusMax) + (bm == n.h.move ? BonusTTMove : 0) +
              f[-1].stat_score / BonusPrevStatDiv;
  const int malus = std::min(MalusMul * n.depth - MalusSub, MalusMax);
  if (!n.pv)
    bonus += (int)((long long)bonus * (n.n_quiets + n.n_tacts) / 256);

  if (!is_tactical(bm)) {
    learn_quiet(td, f, bm, bonus * QuietBonusScale / 1024);
    int m = malus * QuietMalusScale / 1024;
    for (int i = 0; i < n.n_quiets; i++) {
      m = m * QuietMalusDecay / 1024;
      learn_quiet(td, f, n.quiets[i], -m);
    }
  } else
    learn_capture(td, bm, bonus * CaptBonusScale / 1024);

  // La mossa che ha portato qui, se quieta e cercata subito (prima mossa, o seconda dopo la mossa hash), era debole.
  if (n.prev_to >= 0 && f[-1].move_count == 1 + (f[-1].tt_hit ? 1 : 0) && !n.prior_capture)
    learn_followups(f - 1, td.piece_on[n.prev_to], n.prev_to, -malus * PrevContMalus / 1024);

  for (int i = 0; i < n.n_tacts; i++)
    learn_capture(td, n.tacts[i], -malus * CaptMalusScale / 1024);
}

// --------------------------------------------------------------------------
// Fase 1: ingresso del nodo. Ritorna un valore se il nodo si chiude qui.
// --------------------------------------------------------------------------
static int node_open(ThreadData &td, NodeFrame *f, NodeState &n) {
  if (!n.root && n.alpha < 0 && td_upcoming_repetition(td)) {
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
  n.excluded = f->excluded;

  hash_read(td, n.h);
  f->tt_hit = n.h.hit;
  if (n.root)
    n.h.move = td.root[td.pv_idx].pv[0];
  if (!n.excluded)
    f->tt_pv = n.pv || (n.h.hit && n.h.pv);
  n.tact_hash = n.h.move && is_tactical(n.h.move);
  return NO_VALUE;
}

// Taglio dalla hash. Ritorna un valore se si taglia.
static int hash_cutoff(ThreadData &td, NodeFrame *f, NodeState &n) {
  const HashView &h = n.h;
  const bool enough = h.depth > n.depth - (h.value <= n.beta ? 1 : 0);
  if (n.pv || n.excluded || !enough || !val_known(h.value))
    return NO_VALUE;
  const bool high = h.value >= n.beta;
  if (bound_covers(h.bound, high) && (n.cut == high || n.depth > 4)) {
    if (h.move && high) {
      if (!n.tact_hash)
        learn_quiet(td, f, h.move, std::min(112 * n.depth, 695));
      if (n.prev_to >= 0 && f[-1].move_count < 5 && !n.prior_capture)
        learn_followups(f - 1, td.piece_on[n.prev_to], n.prev_to, -2210);
    }
    if (td.fifty >= 96)
      return NO_VALUE;
    // In profondita' si controlla che il figlio della mossa hash confermi lo stesso esito.
    if (n.depth >= 7 && h.move && td_is_pseudo_legal(td, h.move) && !val_decided(h.value)) {
      MoveSafety s;
      s.ready = false;
      if (legal_before(td, s, h.move)) {
        UndoInfo u;
        td.repetition_table[++td.repetition_index] = td.hash_key;
        td.ply++;
        if (td_make_move(td, h.move, u)) {
          int cm, cv, cd, cf, ce;
          bool cp;
          const bool hit = probe_tt(td.hash_key, cm, cv, cd, cf, ce, cp) || cf == BOUND_NIL;
          const int child = hit && cf != BOUND_NIL ? from_hash_value(cv, td.ply, td.fifty) : VAL_NONE;
          td_unmake_move(td, h.move, u);
          td.ply--;
          td.repetition_index--;
          if (!val_known(child) || (h.value >= n.beta) == (-child >= n.beta))
            return h.value;
          return NO_VALUE;
        }
        td.ply--;
        td.repetition_index--;
      }
    }
    return h.value;
  }
  // Nessun taglio pur con un limite profondo che contraddice la finestra: l'entry vale meno.
  if (h.bound != BOUND_EXACT && h.bound == (high ? BOUND_UP : BOUND_LOW) && n.depth > 5)
    hash_soften(td.hash_key);
  return NO_VALUE;
}

// Tablebase nei nodi interni.
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
    f->static_eval = n.eval = corrected_eval(plain, n.signal);
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
  if (n.prior_red >= 2 && n.depth >= 2 && f->static_eval + f[-1].static_eval > 166)
    n.depth--;
}

static int prune_before_moves(ThreadData &td, NodeFrame *f, NodeState &n) {
  const int us = td.side;
  // La differenza di valutazione col ply precedente insegna qualcosa sulla mossa che ci ha portati qui.
  if (move_ok(f[-1].move) && !f[-1].in_check && !n.prior_capture) {
    const int diff = std::clamp(-(f[-1].static_eval + f->static_eval), -189, 194) + 60;
    const int pm = f[-1].move;
    stat_push(td.quiet_hist[us ^ 1][quiet_key(pm)], diff * EvalDiffMul, LIM_QUIET);
    const int pc_now = td.piece_on[n.prev_to];
    if (!n.h.hit && pc_now % 6 != P && !get_move_promoted(pm))
      stat_push(pawn_row(td)[pc_now][n.prev_to], diff * EvalDiffPawnMul, LIM_PAWN);
  }

  // Valutazione molto sotto alpha: basta la quiescenza.
  if (!n.pv && n.eval < n.alpha - RazorMul * n.depth * n.depth)
    return quiesce(td, f, n.alpha, n.beta, false);

  // Valutazione abbastanza sopra beta: il nodo regge senza cercare.
  if (!f->tt_pv && n.depth < (n.hunt_mate ? 6 : RfpMaxDepth) && n.eval >= n.beta && (!n.h.move || n.tact_hash) &&
      !val_lost(n.beta) && !val_won(n.eval)) {
    int mult = std::min(RfpMultBase + n.depth * RfpMultDepth, RfpMultCap);
    mult -= RfpNoTTHit * !f->tt_hit;
    const int margin = mult * n.depth - (RfpImproving * n.improving + RfpOppWorse * n.opp_worse) * mult / 1024 +
                       std::abs(n.signal) / RfpCorrDiv;
    if (n.eval - margin >= n.beta)
      return (RfpBetaWeight * n.beta + (1024 - RfpBetaWeight) * n.eval) / 1024;
  }

  // Mossa nulla: se anche passando la mano si resta sopra beta, il nodo taglia.
  const int non_pawn = td.occupancies[us] & ~td.bitboards[us == white ? P : p] & ~td.bitboards[us == white ? K : k]
                           ? 1 : 0;
  if (n.cut && f->static_eval >= n.beta - NmpDepthMul * n.depth - NmpImproving * n.improving + NmpBase &&
      !n.excluded && non_pawn && td.ply >= td.nmp_min_ply && n.beta >= -2000) {
    const int r = NmpRedBase + n.depth / 3 + std::max((f->static_eval - n.beta) / NmpEvalDiv, 0);
    NullUndo nu;
    step_in_null(td, f, nu);
    f[1].in_check = false;
    const int nv = -explore(td, f + 1, -n.beta, -n.beta + 1, n.depth - r, false, false);
    step_out_null(td, nu);
    if (nv >= n.beta && !val_won(nv)) {
      if (td.nmp_min_ply || n.depth < NmpVerifyDepth)
        return nv;
      td.nmp_min_ply = td.ply + 3 * (n.depth - r) / 4;
      const int v = explore(td, f, n.beta - 1, n.beta, n.depth - r, false, false);
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
      if (m == n.excluded || !legal_before(td, s, m))
        continue;
      const bool gives = checks_before(td, s, m);
      UndoInfo u;
      if (!step_in(td, f, m, u))
        continue;
      f[1].in_check = gives;
      int v = -quiesce(td, f + 1, -cut_beta, -cut_beta + 1, false);
      if (v >= cut_beta && cut_depth > 0)
        v = -explore(td, f + 1, -cut_beta, -cut_beta + 1, cut_depth, !n.cut, false);
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
static bool skip_move(ThreadData &td, NodeFrame *f, NodeState &n, MoveQueue &q, int m, bool tact, bool gives,
                      int r) {
  const int us = td.side;
  if (n.root || val_lost(n.best))
    return false;
  const U64 own_pieces = td.occupancies[us] & ~td.bitboards[us == white ? P : p] & ~td.bitboards[us == white ? K : k];
  if (!own_pieces)
    return false;
  if (f->move_count >= (3 + n.depth * n.depth) / (2 - n.improving))
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
    if ((n.alpha >= 0 || !last_piece) && !see_holds(td, m, -margin))
      return true;
    return false;
  }

  if (f->follow_pv && n.pv)
    return false;
  const int d_index = std::min(n.depth, 16) - 1;
  static const int hist_div[16] = {3637, 2787, 2761, 2939, 3171, 3347, 3147, 2762,
                                   2772, 3106, 3107, 3060, 3112, 2991, 3090, 3542};
  int hist = (*f[-1].cont_hist)[pc][to] + (*f[-2].cont_hist)[pc][to] + pawn_row(td)[pc][to];
  if (hist < -QuietHistPrune * n.depth)
    return true;
  hist += QuietMainHistMul * td.quiet_hist[us][quiet_key(m)] / 32;
  red_depth += hist / hist_div[d_index];
  const int fut = f->static_eval + QuietFutDepth * red_depth + QuietFutAlpha * (f->static_eval > n.alpha) + QuietFutBase;
  if (!n.in_check && red_depth < 12 && fut <= n.alpha) {
    if (n.best <= fut && !val_decided(n.best) && !val_won(fut))
      n.best = fut;
    return true;
  }
  red_depth = std::max(red_depth, 0);
  return !see_holds(td, m, -QuietSeeMul * red_depth * red_depth);
}

// La mossa e' uno spostamento avanti e indietro senza progresso (stessa pedina fra le stesse case)?
static inline bool shuffles(const ThreadData &td, const NodeFrame *f, int m) {
  if (is_tactical(m) || td.fifty < 10 || td.plies_from_null < 6 || f->ply < 20)
    return false;
  const int m2 = f[-2].move, m4 = f[-4].move;
  return move_ok(m2) && move_ok(m4) && move_from(m) == move_to(m2) && move_from(m2) == move_to(m4);
}

// Estensione singolare della mossa hash. Ritorna l'estensione; un valore >= beta chiude il nodo (in *cutoff).
static int singular_probe(ThreadData &td, NodeFrame *f, NodeState &n, int m, int new_depth, int *cutoff) {
  const HashView &h = n.h;
  if (n.root || m != h.move || n.excluded || n.depth < 6 + f->tt_pv || !val_known(h.value) || val_decided(h.value) ||
      !bound_covers(h.bound, true) || h.bound == BOUND_UP || h.depth < n.depth - 3 || shuffles(td, f, m) || n.hunt_mate)
    return 0;
  const int s_beta = h.value - (SingMarginBase + SingMarginTtPv * (f->tt_pv && !n.pv)) * n.depth / 63;
  const int s_depth = new_depth / 2;
  f->excluded = m;
  const int v = explore(td, f, s_beta - 1, s_beta, s_depth, n.cut, false);
  f->excluded = 0;
  if (v < s_beta) {
    const int corr_adj = std::abs(n.signal) / 198368;
    const int dbl = SingDoubleBase + SingDoublePv * n.pv - SingDoubleQuiet * !n.tact_hash - corr_adj -
                    1175 * td.tt_move_hist / 114178 - (f->ply > td.root_depth) * 38;
    const int tpl = SingTripleBase + SingTriplePv * n.pv - SingTripleQuiet * !n.tact_hash + SingTripleTtPv * f->tt_pv -
                    corr_adj - (f->ply > td.root_depth) * 43;
    n.depth++;
    return 1 + (v < s_beta - dbl) + (v < s_beta - tpl);
  }
  if (v >= n.beta && !val_decided(v)) {
    stat_push(td.tt_move_hist, -421 - 110 * n.depth, LIM_TTMOVE);
    if (!n.in_check && v > f->static_eval) {
      const int bonus = std::clamp((v - f->static_eval) * s_depth * 177 / 1024, -LIM_CORR / 4, LIM_CORR / 4);
      corr_learn(td, f, bonus);
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

static int move_loop(ThreadData &td, NodeFrame *f, NodeState &n) {
  const int us = td.side;
  MoveQueue q;
  queue_start(q, td, f, n.h.move, n.depth, n.in_check ? QK_EVADE : QK_MAIN);
  MoveSafety s;
  s.ready = false;
  int value = n.best, m;

  while ((m = queue_next(q)) != 0) {
    if (m == n.excluded || !legal_before(td, s, m))
      continue;
    if (n.root) {
      bool listed = false;
      for (int i = td.pv_idx; i < td.pv_last; i++)
        if (td.root[i].pv[0] == m) { listed = true; break; }
      if (!listed)
        continue;
    }
    f->move_count = ++n.moves;
    const bool tact = is_tactical(m);
    const int pc = move_piece(m), to = move_to(m);
    const bool gives = checks_before(td, s, m);
    int new_depth = n.depth - 1;
    int r = base_reduction(td, n.improving, n.depth, n.moves, n.beta - n.alpha);
    if (f->tt_pv)
      r += LmrTtPvAdd;

    if (skip_move(td, f, n, q, m, tact, gives, r))
      continue;

    int cut_value = NO_VALUE;
    const int ext = singular_probe(td, f, n, m, new_depth, &cut_value);
    if (cut_value != NO_VALUE)
      return cut_value;

    const U64 nodes_before = td.nodes;
    UndoInfo u;
    if (!step_in(td, f, m, u)) {
      f->move_count = --n.moves;
      continue;
    }
    f[1].in_check = gives;
    new_depth += ext;

    if (f->tt_pv)
      r -= LmrTtPvSub + n.pv * LmrTtPvPv + (n.h.value > n.alpha) * LmrTtPvAlpha +
           (n.h.depth >= n.depth) * (LmrTtPvDepth + n.cut * LmrTtPvCut);
    r += LmrOffset;
    r -= n.moves * LmrMoveCount;
    r -= std::abs(n.signal) / LmrCorrDiv;
    if (n.cut)
      r += LmrCutNode + LmrCutNoTT * !n.h.move;
    if (n.tact_hash)
      r += LmrTTCapture;
    if (f[1].cutoff_cnt > 1)
      r += LmrCutoffBase + LmrCutoffMany * (f[1].cutoff_cnt > 2) + LmrCutoffAll * n.all;
    else if (m == n.h.move)
      r -= LmrTTMove;
    if (tact) {
      // Pezzo davvero catturato (per l'en passant e' il pedone, non la casa d'arrivo vuota).
      const int took = u.captured_piece >= 0 ? u.captured_piece % 6 : CAPT_NONE;
      f->stat_score = 873 * unit_value[took] / 128 + td.capt_hist[pc][to][took];
    }
    else
      f->stat_score = (2252 * td.quiet_hist[us][quiet_key(m)] + 1126 * (*f[-1].cont_hist)[pc][to] +
                       1093 * (*f[-2].cont_hist)[pc][to]) / 1024;
    r -= f->stat_score * LmrStatDiv / 4096;
    if (!tact && !val_decided(n.alpha))
      r += 3 * std::clamp(n.alpha - n.eval, -64, 96);
    if (n.all)
      r += r * LmrAllNode / (256 * n.depth + 268);

    if (n.depth >= 2 && n.moves > 1) {
      const int d = std::max(1, std::min(new_depth - r / 1024, new_depth + 2)) + n.pv;
      f->reduction = new_depth - d;
      value = -explore(td, f + 1, -(n.alpha + 1), -n.alpha, d, true, false);
      f->reduction = 0;
      if (value > n.alpha) {
        new_depth += (d < new_depth && value > n.best + DeeperMargin) - (value < n.best + ShallowerMargin);
        if (new_depth > d)
          value = -explore(td, f + 1, -(n.alpha + 1), -n.alpha, new_depth, !n.cut, false);
        learn_followups(f, pc, to, 1334);
      }
    } else if (!n.pv || n.moves > 1) {
      if (!n.h.move)
        r += LmrNoTTFull;
      value = -explore(td, f + 1, -(n.alpha + 1), -n.alpha,
                       new_depth - (r > 5234) - (r > 5487 && new_depth > 2), !n.cut, false);
    }
    if (n.pv && (n.moves == 1 || value > n.alpha)) {
      if (m == n.h.move && ((val_known(n.h.value) && val_decided(n.h.value) && n.h.depth > 0) || n.h.depth > 1))
        new_depth = std::max(new_depth, 1);
      value = -explore(td, f + 1, -n.beta, -n.alpha, new_depth, false, true);
    }
    step_out(td, m, u);

    if (stop_threads.load(std::memory_order_relaxed))
      return 0;
    if (n.root)
      root_record(td, n, m, value, td.nodes - nodes_before, n.moves);

    const int nudge = (value == n.best && td.ply + 2 >= td.root_depth && (int(td.nodes) & 14) == 0 &&
                       !val_won(std::abs(value) + 1)) ? 1 : 0;
    if (value + nudge > n.best) {
      n.best = value;
      if (value + nudge > n.alpha) {
        n.best_move = m;
        if (n.pv)
          pv_extend(td, m);
        if (value >= n.beta) {
          f->cutoff_cnt += (ext < 2) || n.pv;
          break;
        }
        if (n.depth > 3 && n.depth < 12 && !val_decided(value))
          n.depth -= 3;
        n.alpha = value;
      }
    }
    if (m != n.best_move && n.moves <= TRACKED) {
      if (tact) n.tacts[n.n_tacts++] = m;
      else      n.quiets[n.n_quiets++] = m;
    }
  }
  return NO_VALUE;
}

// --------------------------------------------------------------------------
// Fase 4: fine del nodo.
// --------------------------------------------------------------------------
static int node_close(ThreadData &td, NodeFrame *f, NodeState &n) {
  const int us = td.side;
  if (n.best >= n.beta && !val_decided(n.best) && !val_decided(n.alpha))
    n.best = (n.best * n.depth + n.beta) / (n.depth + 1);

  if (!n.moves)
    n.best = n.excluded ? n.alpha : n.in_check ? mated_at(td.ply) : 0;
  else if (n.best_move) {
    reward_best(td, f, n);
    if (!n.pv)
      stat_push(td.tt_move_hist, n.best_move == n.h.move ? 918 : -747, LIM_TTMOVE);
  } else if (!n.prior_capture && n.prev_to >= 0) {
    // Nessuna mossa ha superato alpha: la mossa avversaria che ci ha portati qui era buona.
    int scale = -241 - f[-1].stat_score / 98 + std::min(59 * n.depth, 420) + 186 * (f[-1].move_count > 9) +
                142 * (!n.in_check && n.best <= f->static_eval - 106) +
                159 * (!f[-1].in_check && n.best <= -f[-1].static_eval - 68);
    scale = std::max(scale, 0);
    const int bonus = std::min(150 * n.depth - 85, 1337) * scale;
    const int prev_pc = td.piece_on[n.prev_to];
    learn_followups(f - 1, prev_pc, n.prev_to, bonus * 263 / 16384);
    stat_push(td.quiet_hist[us ^ 1][quiet_key(f[-1].move)], bonus * 215 / 32768, LIM_QUIET);
    if (prev_pc % 6 != P && !get_move_promoted(f[-1].move))
      stat_push(pawn_row(td)[prev_pc][n.prev_to], bonus * 324 / 8192, LIM_PAWN);
  } else if (n.prior_capture && n.prev_to >= 0) {
    stat_push(td.capt_hist[td.piece_on[n.prev_to]][n.prev_to][f[-1].captured % 6], 892, LIM_CAPT);
  }

  if (n.pv)
    n.best = std::min(n.best, n.cap_value);
  if (n.best <= n.alpha)
    f->tt_pv = f->tt_pv || f[-1].tt_pv;

  if (!n.excluded && !(n.root && td.pv_idx))
    hash_write(td, n.best, f->tt_pv,
               n.best >= n.beta ? BOUND_LOW : (n.pv && n.best_move) ? BOUND_EXACT : BOUND_UP,
               n.moves ? n.depth : std::min(max_ply - 1, n.depth + 6), n.best_move, n.eval_raw);

  if (!n.in_check && !(n.best_move && get_move_capture(n.best_move)) && (n.best > f->static_eval) == (n.best_move != 0)) {
    const int bonus = std::clamp((n.best - f->static_eval) * n.depth * (n.best_move ? 12 : 18) / 128,
                                 -LIM_CORR / 4, LIM_CORR / 4);
    corr_learn(td, f, 1061 * bonus / 1024);
  }
  return n.best;
}

// --------------------------------------------------------------------------
static int explore(ThreadData &td, NodeFrame *f, int alpha, int beta, int depth, bool cut, bool pv) {
  if (depth <= 0)
    return quiesce(td, f, alpha, beta, pv);
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
  n.hunt_mate = td.root_depth >= 16 && std::abs(td.root[td.pv_idx].score) >= 2000;
  n.best = -infinity;
  n.best_move = 0;
  n.moves = 0;
  n.cap_value = infinity;
  n.n_quiets = n.n_tacts = 0;

  int v = node_open(td, f, n);
  if (v != NO_VALUE) return v;
  node_evaluate(td, f, n);
  if ((v = hash_cutoff(td, f, n)) != NO_VALUE) return v;
  if ((v = tablebase_probe(td, f, n)) != NO_VALUE) return v;
  if (!n.in_check) {
    if ((v = prune_before_moves(td, f, n)) != NO_VALUE) return v;
  }

  // Limite inferiore dalla hash ben sopra beta: basta quello.
  const int hash_beta = n.beta + ProbCutTTMargin;
  if (bound_covers(n.h.bound, true) && n.h.depth >= n.depth - 4 && n.h.value >= hash_beta && !val_decided(n.beta) &&
      val_known(n.h.value) && !val_decided(n.h.value))
    return hash_beta;

  if ((v = move_loop(td, f, n)) != NO_VALUE)
    return v;
  return node_close(td, f, n);
}
