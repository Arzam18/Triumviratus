// search/14_deepen.inc -- Approfondimento iterativo, gestione del tempo, righe info.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

// ============================================================================
// GESTIONE DEL TEMPO
// Un piano per mossa (tempo ottimale e massimo) e, a ogni iterazione completata, un verdetto: fermarsi o
// continuare, secondo quanto scende la valutazione, quanto e' stabile la mossa migliore e quanti nodi le vanno.
// ============================================================================
struct ClockState {
  int    optimum = 0, maximum = 0;
  double opening_adjust = -1;       // fissato alla prima mossa della partita dal tempo disponibile
  double prev_time_red = 0.85;      // riduzione usata alla mossa precedente
  bool   have_prev = false;         // c'e' una mossa precedente in questa partita?
  int    prev_best = 0, prev_avg = 0;
  int    iter_values[4] = {0, 0, 0, 0};
  int    iter_idx = 0;
  double best_changes = 0;
  bool   increase_depth = true;
};
static ClockState g_clock;

static void clock_memory_reset() {
  g_clock.opening_adjust = -1;
  g_clock.prev_time_red = 0.85;
  g_clock.have_prev = false;
}

int clock_hard_limit() { return g_clock.maximum; }

static void clock_plan(int us, int game_ply, int overhead) {
  if (!g_limits.time[us]) {
    g_clock.optimum = g_clock.maximum = INT_MAX / 2;
    return;
  }
  const double clock = g_limits.time[us], inc = g_limits.inc[us];
  int horizon = g_limits.movestogo ? std::min(g_limits.movestogo, 50) : 50;
  if (clock < 1000 && !g_limits.movestogo)
    horizon = (int)(clock * 0.05);
  const double left = std::max(1.0, clock + inc * (horizon - 1) - (double)overhead * (2 + horizon));
  double opt_share, max_mult;
  if (!g_limits.movestogo) {
    if (g_clock.opening_adjust < 0)
      g_clock.opening_adjust = 0.3272 * std::log10(left) - 0.4141;
    const double log_sec = std::log10(std::max(1.0, clock) / 1000.0);
    const double opt_c = std::min(0.0029869 + 0.00033554 * log_sec, 0.004905);
    const double max_c = std::max(3.3744 + 3.0608 * log_sec, 3.1441);
    opt_share = std::min(0.012112 + std::pow(game_ply + 3.22713, 0.46866) * opt_c, 0.19404 * clock / left) *
                g_clock.opening_adjust;
    max_mult = std::min(6.873, max_c + game_ply / 12.352);
  } else {
    // Idea 8 (04/10/2026): costanti del ramo movestogo come parametri, per uno SPSA dedicato al 40/15 di CCRL. I
    // default riproducono i valori di prima (la divisione IEEE e' arrotondata correttamente: 880/1000.0 == 0.88).
    opt_share = std::min((TmMtgBase / 1000.0 + game_ply / (TmMtgPly / 10.0)) / horizon, TmMtgClock / 1000.0 * clock / left);
    max_mult = TmMtgMaxBase / 1000.0 + TmMtgMaxHorizon / 1000.0 * horizon;
  }
  g_clock.optimum = (int)std::max(1.0, opt_share * left);
  g_clock.maximum = (int)std::max((double)g_clock.optimum, std::min(0.8097 * clock - overhead, max_mult * g_clock.optimum));
  if (g_ponder_option)
    g_clock.optimum += g_clock.optimum / 4;
}

static inline double lerp_clamped(double x, double x0, double x1, double y0, double y1, double lo, double hi) {
  return std::clamp(y0 + (y1 - y0) * (x - x0) / (x1 - x0), lo, hi);
}

// ============================================================================
// RIGHE INFO
// ============================================================================
static void wdl_from_cp(int cp, int &w, int &d, int &l) {
  const double pw = 1.0 / (1.0 + std::exp(-(cp - 100.0) / 100.0));
  const double pl = 1.0 / (1.0 + std::exp(-(-cp - 100.0) / 100.0));
  w = (int)(pw * 1000 + 0.5);
  l = (int)(pl * 1000 + 0.5);
  d = std::max(0, 1000 - w - l);
}

static void print_score(char *out, size_t sz, int v) {
  if (v >= MATE_FLOOR)
    snprintf(out, sz, "mate %d", (mate_value - v + 1) / 2);
  else if (v <= -MATE_FLOOR)
    snprintf(out, sz, "mate %d", -(mate_value + v) / 2);
  else
    snprintf(out, sz, "cp %d", units_to_cp(v));
}

static void report_lines(ThreadData &td, int depth) {
  U64 nodes = 0, tb = 0;
  for (auto &t : thread_data) {
    nodes += t.nodes;
    tb += t.tb_hits;
  }
  const int elapsed = std::max(1, get_time_ms() - g_limits.start_ms);
  const int lines = std::min(g_multipv, td.root_count);
  std::lock_guard<std::mutex> lock(output_mutex);
  for (int i = 0; i < lines; i++) {
    const RootLine &rl = td.root[i];
    const bool use_prev = rl.score == -infinity;
    if (depth == 1 && use_prev && i > 0)
      continue;
    const int d = use_prev ? std::max(1, depth - 1) : depth;
    int v = use_prev ? rl.prev_score : rl.uci_score;
    if (v == -infinity)
      v = 0;
    char sc[32];
    print_score(sc, sizeof sc, v);
    printf("info depth %d seldepth %d multipv %d score %s", d, rl.sel_depth, i + 1, sc);
    if (!use_prev && rl.lower)
      printf(" lowerbound");
    else if (!use_prev && rl.upper)
      printf(" upperbound");
    if (g_show_wdl) {
      int w, dd, l;
      if (v >= MATE_FLOOR) { w = 1000; dd = l = 0; }
      else if (v <= -MATE_FLOOR) { l = 1000; w = dd = 0; }
      else wdl_from_cp(units_to_cp(v), w, dd, l);
      printf(" wdl %d %d %d", w, dd, l);
    }
    printf(" nodes %llu nps %llu hashfull %d tbhits %llu time %d pv", (unsigned long long)nodes,
           (unsigned long long)(nodes * 1000 / elapsed), hashfull(), (unsigned long long)tb, elapsed);
    const int *pv = use_prev ? rl.prev_pv : rl.pv;
    const int len = use_prev ? rl.prev_pv_len : rl.pv_len;
    for (int j = 0; j < len; j++) {
      printf(" ");
      print_move(pv[j]);
    }
    printf("\n");
  }
  fflush(stdout);
}

// ============================================================================
// APPROFONDIMENTO ITERATIVO (un thread)
// ============================================================================
static inline bool line_before(const RootLine &a, const RootLine &b) {
  return a.score != b.score ? a.score > b.score : a.prev_score > b.prev_score;
}

static void settle_lines(ThreadData &td, int from, int to) {
  std::stable_sort(td.root + from, td.root + to, line_before);
}

static void prepare_frames(ThreadData &td) {
  memset(td.frames, 0, sizeof(td.frames));
  for (int i = 0; i < (int)(sizeof(td.frames) / sizeof(td.frames[0])); i++) {
    NodeFrame &fr = td.frames[i];
    fr.cont_hist = &g_stats.follow[0][0][NO_PC][0];
    fr.cont_corr = &td.cont_corr[NO_PC][0];
    fr.captured = -1;
    fr.static_eval = VAL_NONE;
    fr.ply = i - FRAME_OFFSET;
  }
}

static void set_thread_optimism(ThreadData &td, int avg) {
  // optimism nelle unita' della rete: lo score medio torna in quelle unita' prima della formula.
  const int net = (int)((long long)avg * 1024 / EvalUnits);
  const int o = OptStrength * net / (std::abs(net) + OptDiv);
  td.opt[td.side] = o;
  td.opt[td.side ^ 1] = -o;
  nn_pos_set_optimism(td.nnpos, td.opt[0], td.opt[1]);
}

static void deepen(ThreadData &td) {
  const bool main = td.thread_id == 0;
  NodeFrame *root = &td.frames[FRAME_OFFSET];
  prepare_frames(td);
  root->in_check = td_is_square_attacked(td, get_ls1b_index(td.bitboards[td.side == white ? K : k]), td.side ^ 1);
  td.root_side = td.side;   // Contempt: la patta vale meno per chi muove alla radice
  td.deep_w = 0;
  td.lt_lmr_log_mul = -1;   // forza la costruzione della tabella del thread alla prima iterazione (root_depth e' 0)
  lt_update(td);

  if (main) {
    for (int &v : g_clock.iter_values)
      v = g_clock.have_prev ? g_clock.prev_best : 0;
    g_clock.iter_idx = 0;
    g_clock.best_changes = 0;
  }
  const int lines = std::min(g_multipv, td.root_count);
  int again = 0;
  double time_red = 1;
  int last_best_depth = 0, last_best_score = -infinity;
  int last_best_pv[max_ply + 4], last_best_len = 0;

  fill_with(&td.low_ply_hist[0][0], (size_t)LOW_PLY_SIZE * QH_SIZE, 102);
  for (int c = 0; c < 2; c++)
    for (int i = 0; i < QH_SIZE; i++)
      td.quiet_hist[c][i] = (int16_t)(td.quiet_hist[c][i] * 729 / 1024);

  int best = -infinity;
  while (td.root_depth + 1 < max_ply && !stop_threads.load(std::memory_order_relaxed) &&
         !(g_limits.depth && main && td.root_depth >= g_limits.depth)) {
    td.root_depth++;
    // Idea 2 (Deep*): peso 0..1024 dei termini "profondi", da DeepKnee a DeepKnee + DeepSpan.
    td.deep_w = std::clamp((td.root_depth - DeepKnee) * 1024 / DeepSpan, 0, 1024);
    lt_update(td);   // rampa "tempo lungo": valori effettivi delle leve Lt* per questa profondita' di radice
    if (main)
      g_clock.best_changes /= 2;
    for (int i = 0; i < td.root_count; i++) {
      RootLine &rl = td.root[i];
      rl.prev_score = rl.score;
      memcpy(rl.prev_pv, rl.pv, sizeof(int) * rl.pv_len);
      rl.prev_pv_len = rl.pv_len;
      rl.prev_exact = i < lines;
    }
    if (!g_clock.increase_depth)
      again++;

    for (td.pv_idx = 0; td.pv_idx < lines; td.pv_idx++) {
      td.pv_last = td.root_count;
      RootLine &cur = td.root[td.pv_idx];
      memcpy(td.last_pv, cur.prev_pv, sizeof(int) * cur.prev_pv_len);
      td.last_pv_len = cur.prev_pv_len;
      td.sel_depth = 0;

      int delta = AspBase + td.thread_id % 8 + (int)(std::llabs(cur.mean_sq) / AspMeanSqDiv);
      const int avg = cur.avg_score;
      int alpha = std::max(avg - delta, -infinity);
      int beta = std::min(avg + delta, (int)infinity);
      set_thread_optimism(td, avg);
      nn_root_sync(td);

      int failed_high = 0;
      for (;;) {
        const int depth = std::max(1, td.root_depth - failed_high - 3 * (again + 1) / 4);
        td.root_delta = beta - alpha;
        best = explore(td, root, alpha, beta, depth, false, true);
        settle_lines(td, td.pv_idx, td.pv_last);
        if (stop_threads.load(std::memory_order_relaxed))
          break;
        U64 total = 0;
        if (main && lines == 1 && (best <= alpha || best >= beta)) {
          for (auto &t : thread_data) total += t.nodes;
          if (total > 10000000)
            report_lines(td, td.root_depth);
        }
        if (best <= alpha) {
          beta = alpha;
          alpha = std::max(best - delta, -infinity);
          failed_high = 0;
          if (main)
            g_stop_at_ponderhit.store(false, std::memory_order_relaxed);
        } else if (best >= beta) {
          alpha = std::max(beta - delta, alpha);
          beta = std::min(best + delta, (int)infinity);
          failed_high++;
        } else
          break;
        delta += AspGrow * delta / 128;
      }

      settle_lines(td, 0, td.pv_idx + 1);
      if (main && !stop_threads.load(std::memory_order_relaxed)) {
        U64 total = 0;
        for (auto &t : thread_data) total += t.nodes;
        if (td.pv_idx + 1 == lines || total > 10000000)
          report_lines(td, td.root_depth);
      }
      if (stop_threads.load(std::memory_order_relaxed))
        break;
    }
    td.pv_idx = 0;

    // Un matto gia' provato non si dimentica: se l'iterazione lo perde, si torna alla linea che lo aveva.
    RootLine &top = td.root[0];
    const bool stopped = stop_threads.load(std::memory_order_relaxed);
    const bool forgot_mate = last_best_score != -infinity && val_mating(last_best_score) &&
                             (std::abs(top.score) < std::abs(last_best_score) || top.lower || top.upper);
    if (!stopped) {
      if (!last_best_len || last_best_pv[0] != top.pv[0])
        last_best_depth = td.root_depth;
      if (!forgot_mate) {
        memcpy(last_best_pv, top.pv, sizeof(int) * top.pv_len);
        last_best_len = top.pv_len;
        last_best_score = top.score;
      }
      td.completed_depth = td.root_depth;
    }
    const bool aborted_loss = stopped && val_lost(top.score) && top.score != -infinity && !top.lower && !top.upper;
    if ((aborted_loss || (top.score != -infinity && forgot_mate)) && last_best_len) {
      for (int i = 0; i < td.root_count; i++)
        if (td.root[i].pv[0] == last_best_pv[0]) {
          RootLine keep = td.root[i];
          for (int j = i; j > 0; j--)
            td.root[j] = td.root[j - 1];
          td.root[0] = keep;
          break;
        }
      td.root[0].score = td.root[0].uci_score = last_best_score;
      memcpy(td.root[0].pv, last_best_pv, sizeof(int) * last_best_len);
      td.root[0].pv_len = last_best_len;
      td.root[0].lower = td.root[0].upper = false;
    }

    if (g_limits.mate && !stopped && val_mating(td.root[0].score) &&
        mate_value - std::abs(td.root[0].score) <= 2 * g_limits.mate)
      stop_threads.store(true, std::memory_order_relaxed);

    if (!main)
      continue;

    for (auto &t : thread_data) {
      g_clock.best_changes += t.best_move_changes;
      t.best_move_changes = 0;
    }

    if (g_limits.use_tm() && !stop_threads.load(std::memory_order_relaxed) &&
        !g_stop_at_ponderhit.load(std::memory_order_relaxed)) {
      const U64 effort = td.root[0].effort * 100000 / std::max<U64>(1, td.nodes);
      const double prev_avg = g_clock.have_prev ? g_clock.prev_avg : (double)infinity;
      double falling = (TmFallBase / 100.0 + TmFallPrev / 100.0 * (prev_avg - best) +
                        TmFallIter / 100.0 * (g_clock.iter_values[g_clock.iter_idx] - best)) / 100.0;
      falling = std::clamp(falling, 0.576, 1.728);
      time_red = lerp_clamped(td.root_depth - last_best_depth, 4.96, 18.79, 0.639, 1.712, 0.629, 1.544);
      const double reduction = (1.468 + g_clock.prev_time_red) / (2.284 * time_red);
      const double instability = TmInstabBase / 1000.0 + TmInstabMul / 1000.0 * g_clock.best_changes / num_threads;
      const double heavy = lerp_clamped((double)effort, 75800, 104510, 0.969, 0.714, 0.693, 0.838);
      double total_time = g_clock.optimum * falling * reduction * instability * heavy;
      if (td.root_count == 1)
        total_time = std::min(500.0, total_time);
      const int elapsed = get_time_ms() - g_limits.start_ms;
      if (elapsed > std::min(total_time, (double)g_clock.maximum) || td.root[lines - 1].score >= mating_in(3) ||
          td.root[0].score == mated_at(2)) {
        if (g_pondering.load(std::memory_order_relaxed))
          g_stop_at_ponderhit.store(true, std::memory_order_relaxed);
        else
          stop_threads.store(true, std::memory_order_relaxed);
      } else
        g_clock.increase_depth = g_pondering.load(std::memory_order_relaxed) || elapsed <= total_time * 0.50;
    }
    g_clock.iter_values[g_clock.iter_idx] = best;
    g_clock.iter_idx = (g_clock.iter_idx + 1) & 3;
  }
  if (main)
    g_clock.prev_time_red = time_red;
}
