// search/14_smp.inc -- Gestione dei thread, driver asincrono, ingresso della ricerca.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// THREAD MANAGEMENT
// ============================================================================

void stop_search_threads() {
  g_uci_stop.store(true, std::memory_order_relaxed);
  stop_threads.store(true, std::memory_order_relaxed);
}

void wait_for_threads() {
  for (auto &t : search_threads) {
    if (t.joinable())
      t.join();
  }
  search_threads.clear();
}

// ============================================================================
// ASYNCHRONOUS SEARCH DRIVER
//
// The UCI "go" must not block the input loop, otherwise "stop" and
// "go infinite" cannot work. So search_position_mt() runs on its own master
// thread (which in turn spawns the helper threads), while the UCI loop keeps
// reading stdin.
// ============================================================================

std::thread search_master;

// Join the master search thread if it is running/finished. Callers that may
// invoke this while an (possibly infinite) search is in progress must first
// call stop_search_threads() so the search actually terminates.
void wait_for_search_done() {
  if (search_master.joinable())
    search_master.join();
}

// Stop any search in progress, wait for it to fully finish, then start a new
// search on a background thread.
void launch_search(int depth) {
  stop_search_threads();  // stop a previous search (if any)
  wait_for_search_done(); // join it and its helpers

  // Syzygy ROOT probe (DTZ): if the GLOBAL (root) position is covered by the
  // installed tablebases, play the perfect move immediately instead of
  // searching. Done HERE, on the main UCI thread and BEFORE spawning the
  // worker, so it cannot race with parse_position() of the next command.
  // Needs .rtbz (DTZ) files; with WDL-only tables it simply fails and we fall
  // through to a normal search (whose in-search WDL probe already fixes the
  // evaluation of covered endgames).
  // In ponder niente scorciatoia: stamperebbe bestmove prima del ponderhit (la ricerca la trova comunque con la WDL).
  if (!g_go_infinite && !g_go_ponder) {
    int tb_move = 0, tb_score = 0;
    if (syzygy_probe_root(tb_move, tb_score)) {
      new_search();
      printf("info depth 1 score cp %d tbhits 1 pv ", tb_score);
      print_move(tb_move);
      printf("\nbestmove ");
      print_move(tb_move);
      printf("\n");
      fflush(stdout);
      return;
    }
  }

  stop_threads.store(false, std::memory_order_relaxed); // clear before spawning
  g_uci_stop.store(false, std::memory_order_relaxed);
  // Dopo il join della ricerca precedente: la sua chiusura azzera g_pondering e non deve cancellare questo.
  g_pondering.store(g_go_ponder, std::memory_order_relaxed);
  search_master = std::thread(search_position_mt, depth);
}

// `go ponder`: parse_go ha appena calcolato il budget (timeset, soft_time_limit, stoptime); lo si mette da parte e la
// ricerca parte senza limiti di tempo. Con `go ponder infinite` o senza orologio timeset resta 0 anche al ponderhit.
void ponder_park_time() {
  g_ponder_timeset = timeset;
  g_ponder_soft = soft_time_limit;
  g_ponder_stop = stoptime;
  timeset = 0;
  g_go_ponder = true;
}

// `ponderhit`: l'avversario ha giocato la mossa prevista, la ricerca diventa normale con il budget parcheggiato.
// timeset si accende per ultimo, dopo una barriera: chi lo vede acceso legge gia' i limiti giusti.
void ponder_hit() {
  if (!g_pondering.load(std::memory_order_acquire))
    return;
  if (g_ponder_timeset) {
    soft_time_limit = g_ponder_soft;
    stoptime = g_ponder_stop;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    timeset = 1;
  }
  g_pondering.store(false, std::memory_order_release);
  // Budget morbido gia' consumato in ponder (come stopOnPonderhit di Stockfish): si muove subito con l'ultima iterazione
  // completata. Senza, l'iterazione in corso, profonda perche' il ponder non aveva limiti, andava avanti fino al limite
  // duro: misurato 82 ms di mediana dopo il ponderhit contro 46 ms di una mossa normale a 5+0.05.
  if (g_ponder_timeset && get_time_ms() >= g_ponder_soft && thread_data[0].best_move)
    stop_threads.store(true, std::memory_order_relaxed);
}

// ============================================================================
// TMDrift (28/09/2026): piu' tempo quando la valutazione SCIVOLA lentamente.
// Le sconfitte della 7.0 su CCRL non sono crolli ma derive: -0,1 alla volta per 10-20 mosse, nel mediogioco,
// e a nessuna singola mossa la gestione del tempo vede un allarme. Qui si tiene lo score di radice di ogni nostra
// mossa della partita (azzerato da ucinewgame) e parse_go allunga l'optimum se, sulle ultime TMDriftMoves mosse,
// lo score e' sceso in totale di almeno TMDriftThresh cp SENZA un singolo calo di quella misura (quello e' un
// errore, non una deriva, e lo gestisce gia' la TM esistente). TMDrift = % in piu' di optimum; 0 = spento.
// ============================================================================
static int g_drift_hist[512];
static int g_drift_n = 0;
void tm_drift_reset() { g_drift_n = 0; }
static void tm_drift_push(int score) {
  if (score > -mate_score && score < mate_score && g_drift_n < 512)
    g_drift_hist[g_drift_n++] = score;
}
int tm_drift_extra_pct() {
  if (!g_tm_drift || g_drift_n < g_tm_drift_moves + 1)
    return 0;
  const int *h = g_drift_hist + g_drift_n - 1 - g_tm_drift_moves;
  if (h[0] - h[g_tm_drift_moves] < g_tm_drift_thresh || h[g_tm_drift_moves] < -600 ||
      h[g_tm_drift_moves] > 300)
    return 0;
  for (int i = 0; i < g_tm_drift_moves; i++)
    if (h[i] - h[i + 1] >= g_tm_drift_thresh)
      return 0; // un calo singolo grosso: non e' deriva
  return g_tm_drift;
}

// ============================================================================
// MAIN SEARCH ENTRY
// ============================================================================

void search_position_mt(int depth) {
#ifdef TRIUMV_FROZEN
  triumv_frozen_check();   // vedi il blocco PARAMETRI CONGELATI
#endif
  search_start_time = get_time_ms();
  g_node_limit_thread =
      g_node_limit ? std::max<U64>(1, g_node_limit / (U64)num_threads) : 0;

  // Increment TT age
  new_search();

  // NOTE: stop_threads is cleared by launch_search() BEFORE this thread is
  // spawned, so that a "stop" arriving right after "go" is not lost here.
  // NOTE: the Syzygy ROOT probe is NOT done here. It reads the GLOBAL board and
  // must run on the main (UCI) thread BEFORE this worker is spawned - otherwise
  // it races with parse_position() of the next command. See launch_search().
  stopped = 0;

  for (int i = 0; i < num_threads; i++) {
    thread_data[i].nodes = 0;
    thread_data[i].tb_hits = 0;
    thread_data[i].best_move = 0;
    thread_data[i].best_reply = 0;
    thread_data[i].best_score = -infinity;
    thread_data[i].depth = 0;
    thread_data[i].fh_nodes = 0;
    thread_data[i].fh_first = 0;
    thread_data[i].fh_move_sum = 0;
    thread_data[i].fh_tt = 0;
    thread_data[i].fh_tt_first = 0;
    thread_data[i].fh_probe = 0;
    memset(thread_data[i].tt_probe_n, 0, sizeof(thread_data[i].tt_probe_n));
    memset(thread_data[i].tt_probe_hit, 0, sizeof(thread_data[i].tt_probe_hit));
    memset(thread_data[i].tt_probe_mv, 0, sizeof(thread_data[i].tt_probe_mv));
  }
  // AUDIT D (S2, 26/09/2026): td_root_move_ready() legge thread_data[0].pv_table[0][0]. Senza questo reset un
  // helper, al primo controllo del tempo, vedeva la PV della ricerca PRECEDENTE e poteva fermare tutto prima che
  // il thread 0 cercasse una sola mossa (budget sotto ~16 ms), giocando la mossa di soccorso.
  thread_data[0].pv_table[0][0] = 0;
  thread_data[0].pv_length[0] = 0;

  search_threads.clear();

  // Start helper threads
  for (int i = 1; i < num_threads; i++) {
    search_threads.emplace_back(thread_search, i, depth);
  }

  // Main thread search
  thread_search(0, depth);

  // infinite, o ponder finito prima del ponderhit (profondita' massima, matto): la bestmove aspetta stop/ponderhit.
  while ((g_go_infinite || g_pondering.load(std::memory_order_acquire)) &&
         !g_uci_stop.load(std::memory_order_relaxed))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  // Prima della bestmove: dopo averla vista la GUI puo' mandare subito il prossimo `go ponder`.
  g_pondering.store(false, std::memory_order_relaxed);

  stop_threads.store(true, std::memory_order_relaxed);
  wait_for_threads();

  // Get best result from all threads
  int best_move = thread_data[0].best_move;
  int best_score = thread_data[0].best_score;
  int best_depth = thread_data[0].depth;

      // Legacy: vince il thread piu' profondo (tie: score piu' alto).
    // AUDIT D (S3, 27/09/2026): un matto NOSTRO provato ha la precedenza sulla profondita', come nel ramo
    // del voto e in SF (vince il matto piu' corto). Prima un helper piu' profondo che non vedeva il matto
    // poteva scavalcare il thread che l'aveva trovato, anche in `go mate`. A 1 thread il ciclo non gira.
    int mate_thread = -1;
    for (int i = 0; i < num_threads; i++)
      if (thread_data[i].best_move && thread_data[i].best_score >= mate_score &&
          (mate_thread < 0 || thread_data[i].best_score > thread_data[mate_thread].best_score))
        mate_thread = i;
    if (mate_thread >= 0) {
      best_move = thread_data[mate_thread].best_move;
      best_score = thread_data[mate_thread].best_score;
      best_depth = thread_data[mate_thread].depth;
    } else
    for (int i = 1; i < num_threads; i++) {
      if (thread_data[i].depth > best_depth ||
          (thread_data[i].depth == best_depth &&
           thread_data[i].best_score > best_score)) {
        if (thread_data[i].best_move != 0) {
          best_move = thread_data[i].best_move;
          best_score = thread_data[i].best_score;
          best_depth = thread_data[i].depth;
        }
      }
    }
  

  // FIX 2026-07-26 — "Bestmove does not match beginning of last PV".
  // La bestmove puo' venire da un thread != 0 (sia col voting, sia col legacy
  // "vince il piu' profondo" qui sopra), ma le info-line le stampa SOLO il
  // thread 0: tutte le chiamate a print_search_info sono guardate da
  // thread_id == 0. Quando vince un helper, l'ultima PV stampata e' quella del
  // thread 0 mentre la mossa giocata e' quella dell'helper -> due ricerche
  // diverse. Le GUI e fastchess lo segnalano, e la PV mostrata in analisi e'
  // fuorviante.
  // ⚠️ NON tocca la forza di gioco: la mossa giocata resta quella scelta dalla
  // regola di selezione, che e' il comportamento voluto. E' un difetto di
  // REPORTING. Si vede solo da oggi perche' e' il primo match multi-thread:
  // tutti i gate fino al 26/07 erano a 1 thread.
  // Effetto collaterale che resta APERTO: la predizione TM qui sotto richiede
  // thread_data[0].pv_table[0][0] == best_move, quindi quando vince un helper
  // si disattiva da sola. Non e' un bug di correttezza, ma con 8 thread quella
  // parte di TMv2 e' di fatto spenta. Farla puntare al thread vincitore
  // cambierebbe il comportamento a >1 thread e va misurato, non fatto qui.
  // A 1 thread la condizione non scatta mai -> byte-identico.
  // Quale thread ha davvero esplorato la variante che stiamo per giocare, cioe' la cui
  // PV comincia con la bestmove. Serve DUE volte: per stampare una PV coerente con la
  // mossa (sotto) e per la predizione TM (piu' giu'). Prima era calcolato solo dentro
  // il ramo della stampa, e la predizione restava incollata al thread 0.
  tm_drift_push(best_score); // TMDrift: storia degli score di radice della partita (search/14_smp.inc)
  int pv_src = (thread_data[0].pv_length[0] > 0 &&
                thread_data[0].pv_table[0][0] == best_move)
                 ? 0
                 : -1;
  if (pv_src < 0) {
    for (int i = 1; i < num_threads; i++) {
      if (thread_data[i].pv_length[0] > 0 &&
          thread_data[i].pv_table[0][0] == best_move) {
        pv_src = i;
        break;
      }
    }
  }

  if (best_move && pv_src != 0) {
    if (pv_src > 0) {
      print_search_info(thread_data[pv_src], best_depth, best_score, 0,
                        thread_data[pv_src].pv_table[0],
                        thread_data[pv_src].pv_length[0]);
    } else {
      // FALLBACK (2026-07-26, seconda passata): puo' non esserci NESSUN thread
      // la cui pv_table[0][0] sia la bestmove. `td.best_move` e `pv_table[0][0]`
      // non sono la stessa cosa: se un'iterazione viene interrotta a meta', la
      // PV e' gia' stata sovrascritta da quella incompleta mentre best_move
      // conserva l'ultima completata; e il mate-restore assegna best_move = cand
      // senza toccare la PV. La prima versione del fix cercava solo un thread
      // corrispondente e in quel caso non stampava nulla -> warning ancora li'.
      // Qui si stampa almeno la mossa come PV di lunghezza 1: e' meno
      // informativo ma e' VERO, e la coerenza bestmove/PV e' garantita sempre.
      print_search_info(thread_data[0], best_depth, best_score, 0, &best_move,
                        1);
    }
  }

  // TM v2 Q-06 (Caissa): salva l'hash della posizione PREVISTA dopo
  // best+risposta (pv[0], pv[1]): al prossimo "go", se l'avversario ha giocato
  // la risposta predetta la TT e' calda -> meno tempo (fattore in parse_go).
  // Usa la PV del thread 0 solo se coincide con la mossa scelta (col voting
  // puo' non esserlo). Le mosse PV sono state fatte/disfatte in QUESTE
  // posizioni durante la search, quindi td_make_move puo' fidarsi dei bit
  // codificati. Stato pulito su ucinewgame.
  if (g_tmv2_pred && g_tmv2_tc_ok) {
    g_tm_pred_hash = 0;
    // FIX 3/08/2026 (dietro TMPredBestThread). La condizione qui sotto vuole che la PV
    // cominci con la mossa scelta: col thread 0 fisso fallisce OGNI VOLTA che vince un
    // helper, e la predizione si spegne da sola. A 4 thread e' successo 536 volte su
    // 5598 partite, cioe' una fetta di TMv2 (+23,8 Elo) e' inattiva proprio nel regime
    // in cui CCRL ci misura. Qui si prende la PV dal thread vincitore.
    // 🔴 SICUREZZA: sotto si fanno make/unmake su `tdp`, quindi la sua posizione DEVE
    // essere la root. Un helper interrotto potrebbe non esserci tornato, e l'hash
    // predetto sarebbe di un'ALTRA posizione — un errore silenzioso che si vedrebbe
    // solo come predizione che non scatta mai. Si confronta l'hash con quello del
    // thread 0 e, se non coincide, si ricade sul comportamento attuale.
    int tsrc = 0;
    ThreadData &tdp = thread_data[tsrc];
    if (best_move && tdp.pv_length[0] >= 2 && tdp.pv_table[0][0] == best_move) {
      int mv0 = tdp.pv_table[0][0], mv1 = tdp.pv_table[0][1];
      UndoInfo u0, u1;
      tdp.ply++;
      tdp.repetition_table[++tdp.repetition_index] = tdp.hash_key;
      if (td_make_move(tdp, mv0, u0)) {
        tdp.ply++;
        tdp.repetition_table[++tdp.repetition_index] = tdp.hash_key;
        if (td_make_move(tdp, mv1, u1)) {
          g_tm_pred_hash = tdp.hash_key;
          td_unmake_move(tdp, mv1, u1);
        }
        tdp.ply--;
        tdp.repetition_index--;
        td_unmake_move(tdp, mv0, u0);
      }
      tdp.ply--;
      tdp.repetition_index--;
    }
  }

  // Self-play data logging (Phase 1): record the searched root position and
  // the chosen move before emitting it. Uses the global board (= root).
  if (g_data_log_enabled && best_move) {
    log_search_record(best_move, best_score, best_depth);
  }

  // Last-resort anti-forfeit: if the search was aborted (extreme time pressure)
  // before thread 0 produced any move or PV, best_move is 0 and we'd emit
  // "bestmove (none)" — an instant loss in any GUI/match. Instead, fall back to
  // the FIRST LEGAL root move so we never forfeit. Mirrors the search's
  // make/unmake protocol (ply + repetition stack) for legality testing.
  if (!best_move && !thread_data[0].pv_table[0][0]) {
    ThreadData &td0 = thread_data[0];
    moves rescue[1];
    td_generate_moves(td0, rescue, false); // all pseudo-legal moves
    for (int i = 0; i < rescue->count; i++) {
      UndoInfo undo;
      td0.ply++;
      td0.repetition_table[++td0.repetition_index] = td0.hash_key;
      if (td_make_move(td0, rescue->moves[i], undo)) {
        td_unmake_move(td0, rescue->moves[i], undo);
        td0.ply--;
        td0.repetition_index--;
        best_move = rescue->moves[i];
        break;
      }
      td0.ply--;
      td0.repetition_index--;
    }
  }

  // CutoffStats (diagnostica move-ordering, off-default): aggrega i contatori
  // di tutti i thread e stampa il first-move-cutoff rate + indice medio della
  // mossa al cutoff.
  if (g_cutoff_stats) {
    U64 fh = 0, first = 0, msum = 0, ftt = 0, ftt1 = 0, fprobe = 0;
    for (int i = 0; i < num_threads; i++) {
      fh += thread_data[i].fh_nodes;
      fprobe += thread_data[i].fh_probe;
      first += thread_data[i].fh_first;
      msum += thread_data[i].fh_move_sum;
      ftt += thread_data[i].fh_tt;
      ftt1 += thread_data[i].fh_tt_first;
    }
    if (fh) {
      U64 nott = fh - ftt, nott1 = first - ftt1;
      printf("info string FMC %.2f%% (fh=%llu first=%llu) avgidx %.3f | ttrate "
             "%.2f%% probehit %.2f%% FMC|tt %.2f%% FMC|nott %.2f%%\n",
             100.0 * (double)first / (double)fh, (unsigned long long)fh,
             (unsigned long long)first, (double)msum / (double)fh,
             100.0 * (double)ftt / (double)fh,
             100.0 * (double)fprobe / (double)fh,
             ftt ? 100.0 * (double)ftt1 / (double)ftt : 0.0,
             nott ? 100.0 * (double)nott1 / (double)nott : 0.0);
    }
    // TTAVAIL: hit-rate dell'entry e disponibilita' della TT-move per fascia di
    // profondita' (1-3, 4-6, 7-9, 10-13, 14+) e totale. Stesso formato della
    // patch di misura in Stockfish, cosi' le due righe si confrontano a colpo d'occhio.
    {
      const int lo[5] = {1, 4, 7, 10, 14}, hi[5] = {3, 6, 9, 13, 63};
      U64 tn = 0, th = 0, tm = 0;
      char buf[512];
      int off = snprintf(buf, sizeof buf, "info string TTAVAIL");
      for (int b = 0; b < 5; b++) {
        U64 n = 0, hh = 0, mm = 0;
        for (int i = 0; i < num_threads; i++)
          for (int d = lo[b]; d <= hi[b]; d++) {
            n += thread_data[i].tt_probe_n[d];
            hh += thread_data[i].tt_probe_hit[d];
            mm += thread_data[i].tt_probe_mv[d];
          }
        tn += n; th += hh; tm += mm;
        off += snprintf(buf + off, sizeof buf - off, " d%d-%d n=%llu hit=%.1f%% mv=%.1f%%",
                        lo[b], hi[b] == 63 ? 99 : hi[b], (unsigned long long)n,
                        n ? 100.0 * hh / n : 0.0, n ? 100.0 * mm / n : 0.0);
      }
      snprintf(buf + off, sizeof buf - off, " | all n=%llu hit=%.1f%% mv=%.1f%%",
               (unsigned long long)tn, tn ? 100.0 * th / tn : 0.0, tn ? 100.0 * tm / tn : 0.0);
      printf("%s\n", buf);
    }
  }

  // AUDIT D (T3/S4, 26/09/2026): stdout non e' bufferizzato e la riga usciva in tre printf libere; un isready
  // concorrente (thread UCI) poteva produrre "bestmove readyok". Stesso mutex delle righe info e del readyok.
  std::lock_guard<std::mutex> out_lock(output_mutex);
  printf("bestmove ");
  if (best_move) {
    print_move(best_move);
    // Ponder (01/10/2026): la risposta prevista e', in ordine, quella dell'ultima iterazione completata del thread
    // della PV (pv_src) o di un altro thread con la stessa bestmove; poi la seconda mossa della PV corrente (che a
    // ricerca fermata a meta' iterazione puo' essere parziale). Senza nessuna delle due la GUI non pondera.
    int reply = 0;
    if (pv_src >= 0 && thread_data[pv_src].best_move == best_move)
      reply = thread_data[pv_src].best_reply;
    for (int i = 0; i < num_threads && !reply; i++)
      if (thread_data[i].best_move == best_move)
        reply = thread_data[i].best_reply;
    if (!reply && pv_src >= 0 && thread_data[pv_src].pv_length[0] >= 2 &&
        thread_data[pv_src].pv_table[0][0] == best_move)
      reply = thread_data[pv_src].pv_table[0][1];
    if (reply) {
      printf(" ponder ");
      print_move(reply);
    }
  } else if (thread_data[0].pv_table[0][0]) {
    print_move(thread_data[0].pv_table[0][0]);
  } else {
    printf(
        "(none)"); // truly no legal move (stalemate/checkmate handled earlier)
  }
  printf("\n");
  fflush(stdout);
}

