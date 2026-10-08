// search/15_threads.inc -- Avvio e arresto della ricerca, mosse di radice, scelta del thread, bestmove.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

void stop_search_threads() {
  g_uci_stop.store(true, std::memory_order_relaxed);
  stop_threads.store(true, std::memory_order_relaxed);
}

void wait_for_threads() {
  for (auto &t : search_threads)
    if (t.joinable())
      t.join();
  search_threads.clear();
}

std::thread search_master;

void wait_for_search_done() {
  if (search_master.joinable())
    search_master.join();
}

// La ricerca gira su un thread a parte, cosi' il ciclo UCI resta libero per stop, ponderhit e isready.
void launch_search(int depth) {
  stop_search_threads();
  wait_for_search_done();
  g_limits.depth = depth;

  // Posizione di radice coperta dalle tablebase (DTZ): si gioca subito la mossa perfetta.
  if (!g_limits.infinite && !g_limits.ponder) {
    int tb_move = 0, tb_score = 0;
    if (syzygy_probe_root(tb_move, tb_score)) {
      new_search();
      std::lock_guard<std::mutex> lock(output_mutex);
      printf("info depth 1 score cp %d tbhits 1 pv ", tb_score);
      print_move(tb_move);
      printf("\nbestmove ");
      print_move(tb_move);
      printf("\n");
      fflush(stdout);
      return;
    }
  }

  stop_threads.store(false, std::memory_order_relaxed);
  g_uci_stop.store(false, std::memory_order_relaxed);
  g_stop_at_ponderhit.store(false, std::memory_order_relaxed);
  g_pondering.store(g_limits.ponder, std::memory_order_relaxed);
  search_master = std::thread(search_position_mt, depth);
}

// L'avversario ha giocato la mossa prevista: la ricerca diventa normale. Se il tempo era gia' finito, si muove.
void ponder_hit() {
  if (!g_pondering.load(std::memory_order_acquire))
    return;
  g_pondering.store(false, std::memory_order_release);
  if (g_stop_at_ponderhit.load(std::memory_order_relaxed))
    stop_threads.store(true, std::memory_order_relaxed);
}

// Mosse legali della radice (ristrette a "go searchmoves" se presenti).
static int collect_root_moves(ThreadData &td, int out[256]) {
  moves ml[1];
  td_generate_moves(td, ml, false);
  int n = 0;
  for (int i = 0; i < ml->count; i++) {
    const int m = ml->moves[i];
    if (g_searchmoves_count) {
      bool listed = false;
      for (int j = 0; j < g_searchmoves_count; j++)
        if (g_searchmoves[j] == m) { listed = true; break; }
      if (!listed)
        continue;
    }
    UndoInfo u;
    td.repetition_table[++td.repetition_index] = td.hash_key;
    td.ply++;
    if (td_make_move(td, m, u)) {
      td_unmake_move(td, m, u);
      out[n++] = m;
    }
    td.ply--;
    td.repetition_index--;
  }
  return n;
}

static void reset_root_lines(ThreadData &td, const int *list, int n) {
  td.root_count = n;
  for (int i = 0; i < n; i++) {
    RootLine &rl = td.root[i];
    rl.pv[0] = list[i];
    rl.pv_len = 1;
    rl.prev_pv_len = 0;
    rl.score = rl.prev_score = rl.avg_score = rl.uci_score = -infinity;
    rl.mean_sq = -(long long)infinity * infinity;
    rl.lower = rl.upper = rl.prev_exact = false;
    rl.sel_depth = 0;
    rl.effort = 0;
  }
}

static void thread_main(int id) {
  deepen(thread_data[id]);
}

// Voto fra i thread: ogni thread vota la propria mossa con un peso che cresce col suo score; un risultato deciso
// e completo (matto o tablebase) prevale.
static ThreadData *pick_thread() {
  ThreadData *best = &thread_data[0];
  if (num_threads == 1)
    return best;
  int floor_score = infinity;
  for (auto &t : thread_data)
    floor_score = std::min(floor_score, t.root[0].score);
  auto votes = [&](int move) {
    long long v = 0;
    for (auto &t : thread_data)
      if (t.root[0].pv[0] == move)
        v += t.root[0].score - floor_score + 14;
    return v;
  };
  for (auto &t : thread_data) {
    const RootLine &b = best->root[0], &c = t.root[0];
    const bool b_dec = b.score != -infinity && val_decided(b.score) && !b.lower && !b.upper;
    const bool c_dec = c.score != -infinity && val_decided(c.score) && !c.lower && !c.upper;
    if (b_dec) {
      if (c_dec && std::abs(c.score) > std::abs(b.score))
        best = &t;
    } else if (c_dec || (!val_lost(c.score) && (votes(c.pv[0]) > votes(b.pv[0]) ||
                                                (votes(c.pv[0]) == votes(b.pv[0]) && c.pv_len > b.pv_len))))
      best = &t;
  }
  return best;
}

// Risposta prevista (TmExpect*, 05/10/2026): chiave della posizione dopo la nostra mossa e la risposta della linea
// principale, sulla scacchiera del thread 0 (alla radice a ricerca finita). 0 se una delle due non e' legale.
static U64 expected_key(ThreadData &td, int first, int reply) {
  UndoInfo u, u2;
  U64 key = 0;
  td.repetition_table[++td.repetition_index] = td.hash_key;
  td.ply++;
  if (td_make_move(td, first, u)) {
    if (td_is_pseudo_legal(td, reply)) {
      td.repetition_table[++td.repetition_index] = td.hash_key;
      td.ply++;
      if (td_make_move(td, reply, u2)) {
        key = td.hash_key;
        td_unmake_move(td, reply, u2);
      }
      td.ply--;
      td.repetition_index--;
    }
    td_unmake_move(td, first, u);
  }
  td.ply--;
  td.repetition_index--;
  return key;
}

// Senza seconda mossa nella linea, la risposta prevista si cerca nella hash.
static int reply_from_hash(ThreadData &td, int first) {
  UndoInfo u;
  int reply = 0;
  td.repetition_table[++td.repetition_index] = td.hash_key;
  td.ply++;
  if (td_make_move(td, first, u)) {
    int m = get_tt_move(tt_key(td));
    if (m && td_is_pseudo_legal(td, m)) {
      UndoInfo u2;
      td.repetition_table[++td.repetition_index] = td.hash_key;
      td.ply++;
      if (td_make_move(td, m, u2)) {
        td_unmake_move(td, m, u2);
        reply = m;
      }
      td.ply--;
      td.repetition_index--;
    }
    td_unmake_move(td, first, u);
  }
  td.ply--;
  td.repetition_index--;
  return reply;
}

void search_position_mt(int depth) {
  (void)depth;
  g_limits.start_ms = starttime;
  build_reduction_table();
  new_search();
  stopped = 0;
  g_poll_countdown = 0;
  g_clock.increase_depth = true;

  int list[256];
  copy_board_to_thread(thread_data[0]);
  // Risposta prevista (TmExpect*): siamo nella posizione attesa alla fine della ricerca precedente?
  g_clock.expected = g_clock.expect_key && thread_data[0].hash_key == g_clock.expect_key;
  g_clock.surprise = g_clock.expect_key && !g_clock.expected;   // TmSurpriseScale
  const int n = collect_root_moves(thread_data[0], list);
  {
    extern int g_move_overhead_ms;
    // TmFenPly (05/10/2026, audit del nucleo): ply di partita come SF, numero di mossa della FEN + mosse giocate.
    // A 0 solo le mosse giocate dopo la posizione data, come prima: da un libro .epd la partita "partiva" da ply 0.
    clock_plan(side, repetition_index + (TmFenPly ? g_fen_ply : 0), g_move_overhead_ms);
  }

  if (n == 0) {
    const bool mated = thread_data[0].frames[FRAME_OFFSET].in_check ||
                       td_is_square_attacked(thread_data[0],
                                             get_ls1b_index(thread_data[0].bitboards[side == white ? K : k]), side ^ 1);
    while ((g_limits.infinite || g_pondering.load()) && !g_uci_stop.load())
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard<std::mutex> lock(output_mutex);
    printf("info depth 0 score %s\nbestmove (none)\n", mated ? "mate 0" : "cp 0");
    fflush(stdout);
    return;
  }

  for (int i = 0; i < num_threads; i++) {
    ThreadData &td = thread_data[i];
    if (i)
      copy_board_to_thread(td);
    td.nodes = td.tb_hits = td.best_move_changes = 0;
    td.nmp_min_ply = 0;
    td.root_depth = td.completed_depth = 0;
    td.pv_idx = 0;
    td.sel_depth = 0;
    reset_root_lines(td, list, n);
  }

  search_threads.clear();
  for (int i = 1; i < num_threads; i++)
    search_threads.emplace_back(thread_main, i);
  deepen(thread_data[0]);

  // "go infinite" e ponder: la bestmove aspetta stop o ponderhit.
  while ((g_limits.infinite || g_pondering.load(std::memory_order_acquire)) &&
         !g_uci_stop.load(std::memory_order_relaxed))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  g_pondering.store(false, std::memory_order_relaxed);
  stop_threads.store(true, std::memory_order_relaxed);
  wait_for_threads();

  ThreadData *bt = g_limits.depth ? &thread_data[0] : pick_thread();
  RootLine &line = bt->root[0];
  g_clock.prev_best = line.score;
  g_clock.prev_avg = line.avg_score;
  g_clock.have_prev = true;
  if (bt != &thread_data[0])
    report_lines(*bt, std::max(1, bt->completed_depth));

  const int best_move = line.pv[0];
  int reply = line.pv_len > 1 ? line.pv[1] : 0;
  if (!reply)
    reply = reply_from_hash(*bt, best_move);

  // Risposta prevista (TmExpect*): serve una linea di almeno tre mosse (nostra, risposta, nostra successiva). Si
  // calcola solo con l'opzione accesa, cosi' a opzione spenta nulla cambia.
  g_clock.expect_key = 0;
  if ((TmExpectScale < 1000 || TmSurpriseScale > 1000) && line.pv_len >= 3) {
    g_clock.expect_key = expected_key(thread_data[0], best_move, line.pv[1]);
    g_clock.expect_move = line.pv[2];
    g_clock.expect_score = line.score;
    g_clock.expect_depth = bt->completed_depth;
  }
  thread_data[0].best_move = best_move;
  thread_data[0].best_score = line.score;
  thread_data[0].best_reply = reply;
  thread_data[0].depth = bt->completed_depth;

  if (g_data_log_enabled && best_move)
    log_search_record(best_move, line.score, bt->completed_depth);

  std::lock_guard<std::mutex> lock(output_mutex);
  printf("bestmove ");
  print_move(best_move);
  if (reply) {
    printf(" ponder ");
    print_move(reply);
  }
  printf("\n");
  fflush(stdout);
}
