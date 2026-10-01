// search/13_iterdeep.inc -- Info UCI e iterative deepening.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// UCI INFO
// ============================================================================

// multipv: 0 = gioco normale (token omesso, output storico invariato);
// k>=1 = analisi MultiPV, emette " multipv k" dopo il seldepth (ordine UCI
// standard). pv_override: stampa questa PV invece di td.pv_table[0] (serve al
// sort MultiPV, dove le linee vengono raccolte e stampate DOPO, in ordine di
// score).
static void print_search_info(ThreadData &td, int depth, int score,
                              int multipv = 0, const int *pv_override = nullptr,
                              int pv_len_override = -1) {
  int elapsed = get_time_ms() - search_start_time;
  if (elapsed == 0)
    elapsed = 1;

  char mpvtok[24] = "";
  if (multipv > 0)
    snprintf(mpvtok, sizeof(mpvtok), " multipv %d", multipv);

  // hashfull: sempre (economico, campiona 1000 slot). WDL: solo se UCI_ShowWDL.
  // Token pronti da inserire nell'info-line (ordine UCI: score .. wdl .. tbhits
  // .. hashfull .. pv).
  char hftok[24];
  snprintf(hftok, sizeof(hftok), " hashfull %d", hashfull());

  char wdltok[48] = "";
  if (g_show_wdl) {
    int W, D, L;
    if (score > mate_score && score < mate_value) {
      W = 1000;
      D = 0;
      L = 0;
    } // matto nostro
    else if (score > -mate_value && score < -mate_score) {
      W = 0;
      D = 0;
      L = 1000;
    } // veniamo mattati
    else {
      int es = nn_get_eval_scale();
      double cpn = (es == 100) ? (double)score : (double)score * 100.0 / es;
      cpn = cpn * 100.0 /
            449.0; // stessa normalizzazione del display cp (NORM_CP, ricalibrato 10/09/2026)
      const double b = 100.0;
      double w = 1.0 / (1.0 + exp(-(cpn - 100.0) / b));
      double l = 1.0 / (1.0 + exp(-(-cpn - 100.0) / b));
      W = (int)(w * 1000.0 + 0.5);
      L = (int)(l * 1000.0 + 0.5);
      D = 1000 - W - L;
      if (D < 0) {
        int s = W + L;
        W = W * 1000 / s;
        L = 1000 - W;
        D = 0;
      }
    }
    snprintf(wdltok, sizeof(wdltok), " wdl %d %d %d", W, D, L);
  }

  U64 total = 0, tbhits = 0;
  for (int i = 0; i < num_threads; i++) {
    total += thread_data[i].nodes;
    tbhits += thread_data[i].tb_hits;
  }

  U64 nps = (total * 1000) / elapsed;

  std::lock_guard<std::mutex> lock(output_mutex);

  if (score > -mate_value && score < -mate_score) {
    // F-014 (2026-07-03): tolto il "-1" (off-by-one ereditato da BBC): mattati
    // in N semimosse (score = -mate_value + N, N pari) -> UCI "mate -(N/2)",
    // non -(N/2)-1.
    printf("info depth %d seldepth %d%s score mate %d%s nodes %llu nps %llu "
           "time %d tbhits %llu%s pv ",
           depth, td.seldepth, mpvtok, -(score + mate_value) / 2, wdltok, total,
           nps, elapsed, tbhits, hftok);
  } else if (score > mate_score && score < mate_value) {
    printf("info depth %d seldepth %d%s score mate %d%s nodes %llu nps %llu "
           "time %d tbhits %llu%s pv ",
           depth, td.seldepth, mpvtok, (mate_value - score) / 2 + 1, wdltok,
           total, nps, elapsed, tbhits, hftok);
  } else {
    // Normalizza 'score cp' alla scala-pedone (SF-style): la search lavora sul
    // valore COMPRESSO da EvalScale, ma il display va riportato alla scala vera
    // -> cp = score*100/EvalScale. No-op a EvalScale=100 (display 5.0
    // invariato); a 56 ri-gonfia il numero deflazionato.
    int es = nn_get_eval_scale();
    int cp = (es == 100) ? score : (int)((long long)score * 100 / es);
    // [5.1 release 2026-07-03] Normalizzazione WDL del DISPLAY (SF
    // NormalizeToPawnValue-style): la scala intrinseca di rubicon-alea e' ~4x
    // gonfiata rispetto alla convenzione "100cp = 50% probabilita' di
    // vittoria". SOLO display: search/TM/TT intatti.
    // 🔴 RICALIBRATO il 10/09/2026 per legio-septima: 392 -> 449. Il 392 veniva da un
    // fit su 191.514 posizioni di partite della 5.1, cioe' della rete rubicon-alea; la
    // 7.0 ha un'altra rete e le costanti della miscela ritarate. Rifatto il fit su
    // 84.660 partite 7.0 contro 7.0 (10,7 milioni di posizioni, profondita' >= 10):
    // P(win)=50% a +118 cp mostrati e P(loss)=50% a -111, media simmetrica 115,
    // quindi 392 x 115/100 = 449. Stabile col time control: 452 a 10+0.1, 447 a 20+0.2,
    // 444 a 25+0.25. Lo zero era gia' centrato (fra -10 e +30 cp patte al 94-98%).
    constexpr int NORM_CP = 449;
    cp = (int)((long long)cp * 100 / NORM_CP);
    printf("info depth %d seldepth %d%s score cp %d%s nodes %llu nps %llu time "
           "%d tbhits %llu%s pv ",
           depth, td.seldepth, mpvtok, cp, wdltok, total, nps, elapsed, tbhits,
           hftok);
  }

  const int *pv = pv_override ? pv_override : td.pv_table[0];
  int pvlen = pv_override ? pv_len_override : td.pv_length[0];
  for (int i = 0; i < pvlen; i++) {
    print_move(pv[i]);
    printf(" ");
  }
  printf("\n");
  fflush(stdout);
}

// ============================================================================
// ITERATIVE DEEPENING
// ============================================================================

// Lazy SMP per-thread depth-skip pattern (Stockfish-style). Helper threads skip
// some iterations so they spend their time at different depths than the main
// thread, filling the shared TT from varied depths instead of duplicating work.
static const int LSMP_SkipSize[8] = {1, 1, 2, 2, 2, 3, 3, 3};
static const int LSMP_SkipPhase[8] = {0, 1, 0, 1, 2, 0, 1, 2};

static void thread_search(int thread_id, int max_depth) {
  ThreadData &td = thread_data[thread_id];

  copy_board_to_thread(td);
  memset(td.chk_hint, -1, sizeof(td.chk_hint)); // NPS 25/09: nessun suggerimento valido
  // OptPerThread: si parte dall'optimism corrente (lasciato dalla ricerca precedente), come oggi.
  td.opt[0] = g_optimism[0];
  td.opt[1] = g_optimism[1];
  if (g_opt_per_thread)
    nn_pos_set_optimism(td.nnpos, td.opt[0], td.opt[1]);

  memset(td.killer_moves, 0, sizeof(td.killer_moves));
  // RIMUOVE L'AMNESIA: la history persiste tra le mosse della partita, cosi'
  // l'ordinamento e' intelligente fin dal primo ms (la gravity in
  // td_update_history evita la saturazione).
  // memset(td.history_moves, 0, sizeof(td.history_moves));
  memset(td.counter_moves, 0, sizeof(td.counter_moves));
  // memset(td.continuation_history, 0, sizeof(td.continuation_history));
  memset(td.pv_table, 0, sizeof(td.pv_table));
  memset(td.pv_length, 0, sizeof(td.pv_length));
  // #5 low-ply history e' ply-indexed near-root -> NON deve persistere tra
  // mosse (mischierebbe posizioni diverse allo stesso ply). Azzerata per-search
  // se attiva (gated: OFF = nessun lavoro extra = byte-identico).
  if (g_lowply)
    memset(td.lowply_history, 0, sizeof(td.lowply_history));
  td.move_stack[0] = 0; // root has no previous move

  int prev_score = 0;
  int prev_best_move = 0;
  int prev_completed_score =
      0;                    // last completed iteration's score (TimeMgmt drop)
  int avg_score = infinity; // F-018.5a AspAvg: media mobile degli score
                            // (infinity = non ancora inizializzata)
  // Snapshot di avg_score PRIMA che assorba lo score corrente. Serve solo al
  // counter Q-03: vedi il commento a TMv2EvalPrevAvg (~:9660).
  int prev_avg_score = infinity;
  long long avg_sq_score = 0; // Q-28 AspScoreMult: EMA di score*|score| (valida
                              // sse avg_score != infinity)
  td.prev_pv_len =
      0; // Q-15: nessuna PV precedente alla prima iterazione di questa search
  int stable_iters =
      0; // T-01 TMStability: iterazioni consecutive con best move invariata
  int eval_stab_iters = 0; // TM v2 Q-03: iterazioni consecutive con score entro
                           // +-window da avg_score (cap 4)
  int mate_score_iters =
      0; // TM v2 Q-08b: iterazioni consecutive con score di matto
  int root_single_reply =
      0; // TM v2 Q-08a: 1 se a root c'e' UNA sola mossa legale

  // TM v2 Q-08a (Caissa): conta le mosse legali a root (si ferma a 2). Con una
  // sola risposta legale pensare e' inutile: basta la prima iterazione
  // completata. Stesso protocollo make/unmake del rescue anti-forfeit (ply +
  // repetition stack).
  // FIX 2026-07-26: tolto `&& g_tmv2_tc_ok`. Quel gate vale `inc > 0` e serve
  // ai fattori di ALLOCAZIONE di TMv2, che presuppongono formule pool-incremento.
  // Questo pero' non alloca niente: se a root c'e' UNA SOLA mossa legale,
  // pensare di piu' non puo' cambiare la mossa — vero con o senza incremento.
  // Conta perche' **CCRL 40/4 e CEGT girano a inc=0**: e' proprio il regime dove
  // veniamo valutati, ed era l'unico in cui questa sicurezza restava spenta.
  // Puo' solo fermarsi PRIMA, mai spendere di piu' -> non puo' causare forfeit.
  if (g_tmv2_single_reply && thread_id == 0 && timeset) {
    moves ml[1];
    td_generate_moves(td, ml, false);
    int legal = 0;
    for (int i = 0; i < ml->count && legal < 2; i++) {
      UndoInfo undo;
      td.ply++;
      td.repetition_table[++td.repetition_index] = td.hash_key;
      if (td_make_move(td, ml->moves[i], undo)) {
        td_unmake_move(td, ml->moves[i], undo);
        legal++;
      }
      td.ply--;
      td.repetition_index--;
    }
    root_single_reply = (legal == 1);
  }

  // MultiPV (analisi): quante linee cercare. Solo main thread; clampato alle
  // mosse LEGALI di root (con meno mosse che linee, la linea in eccesso non ha
  // mosse da cercare e tornerebbe uno score di matto/stallo fasullo). Stesso
  // protocollo make/unmake del conteggio Q-08a sopra.
  int mpv_nlines = 1;
  td.mpv_count = 0;
  if (g_multipv > 1 && thread_id == 0) {
    moves ml[1];
    td_generate_moves(td, ml, false);
    int legal = 0;
    for (int i = 0; i < ml->count; i++) {
      // con searchmoves attivo, contano solo le mosse in whitelist
      if (g_searchmoves_count) {
        bool in_list = false;
        for (int k = 0; k < g_searchmoves_count; k++)
          if (g_searchmoves[k] == ml->moves[i]) {
            in_list = true;
            break;
          }
        if (!in_list)
          continue;
      }
      UndoInfo undo;
      td.ply++;
      td.repetition_table[++td.repetition_index] = td.hash_key;
      if (td_make_move(td, ml->moves[i], undo)) {
        td_unmake_move(td, ml->moves[i], undo);
        legal++;
      }
      td.ply--;
      td.repetition_index--;
    }
    mpv_nlines = g_multipv < legal ? g_multipv : legal;
    if (mpv_nlines < 1)
      mpv_nlines = 1;
  }

  // MultiPV: buffer per raccogliere le linee secondarie dell'iterazione
  // corrente e stamparle ORDINATE per score (mai un multipv k+1 sopra il k
  // nelle GUI).
  struct MpvLine {
    int score;
    int len;
    int pv[max_ply + 8];
  };
  std::vector<MpvLine> mpv_buf;
  if (mpv_nlines > 1)
    mpv_buf.resize(mpv_nlines);

  // Stagger starting depths for thread diversity
  int start_depth = 1 + (thread_id % 2);

  // DiverseSMP (#2), WIDER-ONLY variant: per-thread LMR reduction bias. Helpers
  // (id>0) only ever search WIDER (negative bias = less reduction); the
  // magnitude grows with id but is capped at g_diverse_smp_amount. Thread 0
  // stays canonical (0). RATIONALE: an earlier symmetric (+/-) version hijacked
  // the root move -- a helper with a *positive* bias prunes harder, reaches a
  // higher nominal depth, and wins the depth-based result selection with an
  // over-reduced (worse) move. amt3 measured -79 Elo @8+0.08 from exactly this.
  // Forcing bias <= 0 means every helper reaches a LOWER depth than thread 0,
  // so it can never hijack the result; helpers contribute only via the shared
  // TT (seeding less-pruned entries). For inter-helper diversity run
  // DiverseSMPAmount >= 2 (else all helpers collapse to -1). Re-read each
  // search so DiverseSMPAmount (SPSA) takes effect live. Applied in the LMR
  // block only when g_diverse_smp is on (guarded there).
  if (thread_id == 0) {
    td.lmr_bias = 0;
    td.lmr_bias_units = 0;
  } else if (g_diverse_smp_fine > 0) {
    // DiverseSMPFine: perturbazione FINE (millesimi di ply), wider-only come
    // sopra. Stessa crescita per coppie di id, cap 8 (max ~0.75 ply con
    // fine=96).
    int mag = 1 + (thread_id - 1) / 2;
    if (mag > 8)
      mag = 8;
    td.lmr_bias = 0; // il path legacy (ply interi) resta spento
    td.lmr_bias_units = -mag * g_diverse_smp_fine;
  } else {
    int mag = 1 + (thread_id - 1) / 2;
    if (mag > g_diverse_smp_amount)
      mag = g_diverse_smp_amount;
    td.lmr_bias =
        -mag; // wider-only: helpers never search "deeper" -> no hijack
    td.lmr_bias_units = td.lmr_bias * 1024; // identico al vecchio lmr_bias*1024
  }

  for (int current_depth = start_depth; current_depth <= max_depth;
       current_depth++) {
    if (stop_threads.load(std::memory_order_relaxed))
      break;

    // Lazy SMP: helper threads (id > 0) skip a per-thread pattern of depths so
    // they diversify which iterations they invest in. The main thread (id 0)
    // never skips: it drives time management and prints the PV, so it must
    // progress through every depth monotonically.
    if (thread_id > 0) {
      int s = (thread_id - 1) % 8;
      if (((current_depth + LSMP_SkipPhase[s]) / LSMP_SkipSize[s]) & 1)
        continue;
    }

    // Re-sync the incremental NNUE mirror to the root board (full refresh)
    // so a previously aborted iteration cannot leave the accumulator stack
    // out of step with the search.
    nn_root_sync(td);

    td.root_depth =
        current_depth; // F-018.6c SingularPlyGuard: gate ply < 2*rootDepth
    if (mpv_nlines > 1)
      mpv_buf[0].len =
          0; // MultiPV: nessuna principale deferita da iterazioni stale

    // Aspiration window: start narrow around the previous score and widen
    // incrementally on failures, instead of re-searching the full window.
    // F-018.5a AspAvg (default OFF): centro = media mobile degli score invece
    // del prev_score secco -> il rumore iterazione-su-iterazione non sposta la
    // finestra.
    int alpha = -infinity, beta = infinity, delta = g_asp_init_delta;
    if (g_asp_stable_shrink) { // ricerca stabile -> parti piu' stretto
                               // (Reckless 4192617)
      int stab =
          eval_stab_iters < stable_iters ? eval_stab_iters : stable_iters;
      if (stab > g_asp_stable_cap)
        stab = g_asp_stable_cap;
      delta -= stab;
      if (delta < 1)
        delta = 1;
    }
    // Q-28 AspScoreMult: allarga il delta iniziale col quadrato-EMA dello score
    // (i64: |avgSq| fino a ~9e8, *mult sfora i32). 0 = OFF.
    if (g_asp_score_mult > 0 && avg_score != infinity) {
      long long widen = (avg_sq_score < 0 ? -avg_sq_score : avg_sq_score) *
                            (long long)g_asp_score_mult >>
                        20;
      delta += (int)widen;
    }
    if (current_depth >= 4) {
      int center =
          (g_asp_avg && avg_score != infinity) ? avg_score : prev_score;
      alpha = (center - delta > -infinity) ? center - delta : -infinity;
      beta = (center + delta < infinity) ? center + delta : infinity;
    }

    int score;
    U64 iter_nodes = 0;
    int fail_high_cnt = 0; // F-018.5b AspFHRed
    while (true) {
      U64 nodes_before_call = td.nodes;
      // F-018.5b AspFHRed (default OFF): ogni fail-high consecutivo ri-cerca a
      // depth-1 (Obsidian/Berserk/SF: la conferma del fail-high non serve a
      // profondita' piena).
      int search_depth = current_depth;
      if (g_asp_fhred && fail_high_cnt > 0) {
        search_depth = current_depth - fail_high_cnt;
        if (search_depth < 1)
          search_depth = 1;
      }
      score = td_negamax(td, alpha, beta, search_depth, false, 0,
                         g_follow_pv); // Q-15: la root segue sempre la PV
      iter_nodes =
          td.nodes - nodes_before_call; // nodes of the last (in-window) search
      if (stop_threads.load(std::memory_order_relaxed))
        break;

      if (score <= alpha) { // fail low: lower alpha, pull beta toward midpoint
        beta = (alpha + beta) / 2;
        alpha = (score - delta > -infinity) ? score - delta : -infinity;
        delta += delta * g_asp_grow / 100;
        fail_high_cnt = 0; // F-018.5b: la depth piena torna sul fail-low (SF)
      } else if (score >= beta) { // fail high: raise beta
        beta = (score + delta < infinity) ? score + delta : infinity;
        delta += delta * g_asp_grow / 100;
        if (score < mate_score)
          fail_high_cnt++;
        // Q-31 (Reckless 0010617): score diventato decisivo -> non trascinare
        // le riduzioni AspFHRed accumulate sui fail-high precedenti (il matto
        // va confermato quasi a depth piena, non a depth-N).
        else if (fail_high_cnt > 1)
          fail_high_cnt = 1;
      } else {
        break; // score is inside the window
      }
    }

    if (stop_threads.load(std::memory_order_relaxed))
      break;

    prev_score = score;
    // Q-28: aggiorna l'EMA quadratica PRIMA di avg_score (usa avg_score come
    // flag first-time)
    {
      long long sq = (long long)score * (score < 0 ? -score : score);
      avg_sq_score = (avg_score == infinity) ? sq : (avg_sq_score + sq) / 2;
    }
    prev_avg_score = avg_score; // snapshot per il counter Q-03, vedi ~:9660
    avg_score =
        (avg_score == infinity) ? score : (avg_score + score) / 2; // F-018.5a
    // EvalOptimism (5.1): aggiorna il contempt dinamico per i sottoalberi
    // successivi (SF lo ricava dall'averageScore della root). Solo main thread
    // scrive (gli helper leggono soft).
    if (g_eval_optimism && thread_id == 0) {
      int a = score < 0 ? -score : score;
      int opt = g_opt_strength * score / (a + g_opt_div);
      g_optimism[td.side] = opt;
      g_optimism[td.side ^ 1] = -opt;
    }
    // OptPerThread: ogni thread (helper compresi) aggiorna l'optimism della propria posizione NNUE dal
    // PROPRIO score, invece di leggere quello che il thread 0 riscrive a meta' ricerca (audit D, S5).
    if (g_eval_optimism && g_opt_per_thread) {
      int a = score < 0 ? -score : score;
      int opt = g_opt_strength * score / (a + g_opt_div);
      td.opt[td.side] = opt;
      td.opt[td.side ^ 1] = -opt;
      nn_pos_set_optimism(td.nnpos, td.opt[0], td.opt[1]);
    }

    if (td.pv_length[0] > 0) {
      // Q-29 (Reckless fec9098, default OFF): "forgotten mate" — un'iterazione
      // completata puo' regredire su un matto GIA' provato (collisione TT /
      // potatura). Se lo score precedente era decisivo e il nuovo e' piu'
      // debole in valore assoluto, manteniamo mossa+score dell'iterazione
      // precedente.
      int prev_abs = td.best_score < 0 ? -td.best_score : td.best_score;
      int new_abs = score < 0 ? -score : score;
      if (g_root_mate_restore && td.depth > 0 && prev_abs >= mate_score &&
          new_abs < prev_abs) {
        if (thread_id == 0)
          print_search_info(td, current_depth, td.best_score,
                            mpv_nlines > 1 ? 1 : 0);
      } else {
        td.best_move = td.pv_table[0][0];
        td.best_score = score;
        td.depth = current_depth;
        td.best_reply = td.pv_length[0] >= 2 ? td.pv_table[0][1] : 0;   // ponder

        // Q-15 FollowPV: salva la PV di QUESTA iterazione completata; la
        // prossima iterazione la riseguira' (niente IIR/shallow-pruning su
        // quella linea). Copia solo a iterazione in-window (qui).
        if (g_follow_pv) {
          int len = td.pv_length[0];
          if (len > max_ply)
            len = max_ply;
          for (int i = 0; i < len; i++)
            td.prev_pv[i] = td.pv_table[0][i];
          td.prev_pv_len = len;
        }

        // Only main thread prints
        if (thread_id == 0) {
          if (mpv_nlines > 1) {
            // MultiPV: stampa DEFERITA al blocco sotto — la principale entra
            // nel sort insieme alle secondarie (una secondaria a finestra
            // piena puo' superarla: multipv 1 = sempre la linea migliore).
            mpv_buf[0].score = score;
            mpv_buf[0].len = td.pv_length[0];
            for (int i = 0; i < td.pv_length[0]; i++)
              mpv_buf[0].pv[i] = td.pv_table[0][i];
          } else
            print_search_info(td, current_depth, score);
        }
      }
    }

    // ---- MultiPV: linee secondarie (solo analisi, mpv_nlines>1, thread 0)
    // ---- Dopo la linea principale dell'iterazione, ri-cerca la root a
    // finestra PIENA escludendo le best move gia' assegnate (skip nel move loop
    // di td_negamax via td.mpv_count). Niente aspirazione sulle secondarie:
    // costo accettabile in analisi, zero stato da trascinare tra iterazioni.
    // Tutto lo stato di gioco (best_move/score, TM, FollowPV, optimism, EMA) e'
    // gia' stato aggiornato SOPRA dalla sola linea principale e non viene
    // toccato. ponytail: niente sort per score — l'esclusione rende le linee
    // gia' non-crescenti a meno di rumore; aggiungere il sort se una GUI si
    // confonde.
    if (mpv_nlines > 1 && thread_id == 0 && td.pv_length[0] > 0 &&
        !stop_threads.load(std::memory_order_relaxed)) {
      // done = linee nel buffer da ordinare; base = indice multipv della prima.
      // Path normale: buf[0] = principale (deferita sopra) -> stampa 1..N tutte
      // ordinate. Path mate-restore (Q-29, gia' stampata come singola): base 2,
      // si ordinano solo le secondarie.
      int done = (mpv_buf[0].len > 0) ? 1 : 0;
      int base = done ? 1 : 2;
      td.mpv_excluded[0] = td.pv_table[0][0];
      for (int pvIdx = 1; pvIdx < mpv_nlines; pvIdx++) {
        td.mpv_count = pvIdx;
        nn_root_sync(
            td); // mirror NNUE ri-allineato alla root prima di ri-cercare
        int s2 =
            td_negamax(td, -infinity, infinity, current_depth, false, 0, false);
        if (stop_threads.load(std::memory_order_relaxed) || td.pv_length[0] < 1)
          break;
        td.mpv_excluded[pvIdx] = td.pv_table[0][0];
        MpvLine &L = mpv_buf[done++];
        L.score = s2;
        L.len = td.pv_length[0];
        for (int i = 0; i < L.len; i++)
          L.pv[i] = td.pv_table[0][i];
      }
      // sort di indici per score decrescente e stampa multipv base..: mai una
      // linea sotto con score sopra (convenzione SF).
      int ord[64];
      for (int a = 0; a < done; a++)
        ord[a] = a;
      std::sort(ord, ord + done, [&](int x, int y) {
        return mpv_buf[x].score > mpv_buf[y].score;
      });
      for (int a = 0; a < done; a++) {
        const MpvLine &L = mpv_buf[ord[a]];
        print_search_info(td, current_depth, L.score, base + a, L.pv, L.len);
      }
      // bestmove coerente col multipv 1: se una linea a finestra piena ha
      // superato la principale, riallinea best_move/score (solo analisi:
      // mpv_nlines>1 non esiste in partita, TM/voting non toccati altrove).
      if (base == 1 && done > 0) {
        td.best_move = mpv_buf[ord[0]].pv[0];
        td.best_score = mpv_buf[ord[0]].score;
        td.best_reply = mpv_buf[ord[0]].len >= 2 ? mpv_buf[ord[0]].pv[1] : 0;
      }
      td.mpv_count =
          0; // le iterazioni/ricerche successive ripartono senza esclusioni
    }

    // ---- P4 TroubleMaking (solo main thread, posizione persa) ----
    // Candidato = mossa root (≠ best) col max sforzo cumulativo nella NodeCache
    // (stessa entry per best e candidato -> confronto omogeneo). Se lo sforzo
    // e' comparabile (>= Effort% della best) una verifica null-window a depth/2
    // conferma che vale >= score-Margin -> best_move scambiata (score/PV
    // restano quelli teorici: cambio solo la mossa GIOCATA, non il display).
    if (g_trouble && thread_id == 0 && mpv_nlines == 1 && current_depth >= 8 &&
        td.best_move && score < -g_trouble_score && score > -mate_score &&
        !stop_threads.load(std::memory_order_relaxed)) {
      ThreadData::NCEntry *e = nc_try(td, td.hash_key);
      if (e) {
        int cand = 0;
        U64 cn = 0, bn = 0;
        for (int i = 0; i < ThreadData::NC_MOVES && e->mv[i]; i++) {
          if (e->mv[i] == td.best_move)
            bn = e->mv_nodes[i];
          else if (e->mv_nodes[i] > cn) {
            cand = e->mv[i];
            cn = e->mv_nodes[i];
          }
        }
        if (cand && bn && cn * 100 >= bn * (U64)g_trouble_effort) {
          int save_pv[max_ply + 8];
          int save_len = td.pv_length[0];
          memcpy(save_pv, td.pv_table[0], sizeof(save_pv));
          nn_root_sync(td);
          td.root_only_move = cand;
          int target = score - g_trouble_margin;
          int v = td_negamax(td, target - 1, target, current_depth / 2, false);
          td.root_only_move = 0;
          memcpy(td.pv_table[0], save_pv, sizeof(save_pv));
          td.pv_length[0] = save_len;
          if (!stop_threads.load(std::memory_order_relaxed) && v >= target) {
            td.best_move = cand;
            td.best_reply = 0;   // la risposta salvata era quella alla mossa scartata
            // ristampa l'info-line con la mossa scambiata in testa alla PV:
            // le GUI/fastchess confrontano bestmove col primo move della PV
            // (senza questa riga: warning "Bestmove does not match last PV").
            print_search_info(td, current_depth, score, 0, &cand, 1);
          }
        }
      }
    }

    // "go mate N": un'iterazione completata conferma un matto NOSTRO in <= N
    // mosse -> il compito e' assolto, stop (convenzione UCI; GUI/solver di
    // studi).
    if (g_mate_in > 0 && thread_id == 0 && score > mate_score &&
        score < mate_value && (mate_value - score) / 2 + 1 <= g_mate_in) {
      stop_threads.store(true, std::memory_order_relaxed);
      break;
    }

    // Soft time management: the main thread decides whether to start another
    // iteration. If the best move is unstable (just changed), allow more
    // time, up to the hard cap (stoptime).
    if (thread_id == 0 && timeset) {
      int soft = soft_time_limit; // absolute timestamp (starttime + optimum)

      // T-01 TMStability: iterazioni consecutive con best move invariata
      // (prev_best_move tiene ancora il valore dell'iterazione precedente,
      // aggiornato piu' sotto).
      if (current_depth > start_depth && td.best_move == prev_best_move)
        stable_iters++;
      else
        stable_iters = 0;

      // TM v2 Q-03: counter di stabilita' dell'EVAL (bidirezionale: misura se
      // lo score resta fermo attorno alla media mobile, non solo se cala).
      // ⚠️ 2026-07-18: SGANCIATO dal gate TMv2. Era calcolato solo con TMv2
      // attivo (quindi MAI sotto i 15s), ma ora serve anche ad AspStableShrink,
      // che vive a ogni TC. Il CONSUMO da parte di TMv2 e' invariato -> nessun
      // cambio di comportamento con AspStableShrink=false (il counter viene
      // solo tenuto aggiornato invece che a zero).
      // 🔴 TMv2EvalPrevAvg (10/08/2026). `avg_score` e' gia' stato aggiornato a
      // :9468, PRIMA di arrivare qui, quindi ha gia' assorbito lo score corrente:
      //     avg_new = (avg_prec + score)/2
      //     score - avg_new = (score - avg_prec)/2
      // cioe' `ediff` vale META' della distanza dall'iterazione precedente, e
      // `TMv2EvalWindow = 10` in pratica significa 20.
      // 🔑 Che sia un difetto e non una scelta lo dice il vicino di riga: dodici
      // righe sopra `stable_iters` confronta con `prev_best_move`, e il commento
      // avverte che quello e' aggiornato PIU' SOTTO apposta. Due counter di
      // stabilita' affiancati con convenzioni opposte.
      // ⚠️ NON e' invariante: raddoppiando `ediff` il counter scatta molto meno, e
      // `g_tmv2_eval[]` e' tarato CON il dimezzamento. Va acceso INSIEME a una
      // finestra dimezzata (10 -> 5) per restare quasi-invarianti, e poi ritarato.
      // Da solo e' `CMHCPly1` daccapo: il difetto vecchio e' gia' stato assorbito
      // dalle tarature costruite sopra.
      const int eval_ref = g_tmv2_eval_prevavg ? prev_avg_score : avg_score;
      if (current_depth > start_depth && eval_ref != infinity) {
        int ediff = score - eval_ref;
        if (ediff < 0)
          ediff = -ediff;
        if (ediff <= g_tmv2_eval_window) {
          if (eval_stab_iters < 4)
            eval_stab_iters++;
        } else
          eval_stab_iters = 0;
      }

      if (g_tmv2 && g_tmv2_tc_ok) {
        // ---- TM v2 (Q-01): composizione MOLTIPLICATIVA stateless ----
        // Tutti i fattori ricalcolati freschi dalla durata BASE ogni iterazione
        // (niente accumulo -> niente drift), ognuno bidirezionale.
        int si = stable_iters > 4 ? 4 : stable_iters;
        double scale = g_tmv2_stab[si] / 100.0;        // Q-02 extend-AND-reduce
        scale *= g_tmv2_eval[eval_stab_iters] / 100.0; // Q-03 trend-eval
        if (current_depth >= 6 &&
            iter_nodes > 0) { // Q-04 NodeTM con estensione
          double frac = (double)td.root_bestmove_nodes / (double)iter_nodes;
          if (frac > 1.0)
            frac = 1.0;
          double nf =
              (g_tmv2_node_base / 100.0 - frac) * (g_tmv2_node_mult / 100.0);
          double nmin = g_tmv2_node_min / 100.0, nmax = g_tmv2_node_max / 100.0;
          if (nf < nmin)
            nf = nmin;
          if (nf > nmax)
            nf = nmax;
          scale *= nf;
        }
        int dur = soft - starttime; // BASE optimum duration (ms)
        if (dur < 0)
          dur = 0;
        soft = starttime + (int)(dur * scale); // il clamp a stoptime e' sotto
      } else {

        if (current_depth > start_depth && td.best_move != prev_best_move)
          soft += (stoptime - soft_time_limit) * g_tm_instab / 100;

        // Score-drop time extension (TimeMgmt): a falling score vs the previous
        // completed iteration suggests the position is turning against us ->
        // grant extra time, scaled by the drop, capped at ~25% of the headroom.
        if (g_time_mgmt && current_depth > start_depth) {
          int drop = prev_completed_score - score; // >0 = got worse
          if (drop >
              g_tm_drop_thresh) { // F-002: era hardcoded 8/200 (unita'-eval)
            int d = (drop > g_tm_drop_cap) ? g_tm_drop_cap : drop;
            soft += (stoptime - soft_time_limit) * d / g_tm_drop_div;
          }
        }

        // Node-based time management. IMPORTANT: soft is an ABSOLUTE timestamp,
        // so we must scale the DURATION (soft - starttime), never the timestamp
        // itself. REDUCE-ONLY (scale <= 1.0): if the best move dominates the
        // node count we're confident -> stop sooner. We never inflate the
        // duration (that would push soft past stoptime -> burn the whole
        // clock).
        if (g_node_tm && current_depth >= 6 && iter_nodes > 0) {
          double frac = (double)td.root_bestmove_nodes / (double)iter_nodes;
          if (frac > 1.0)
            frac = 1.0;
          double scale =
              g_nodetm_base / 100.0 - frac; // F-002: era hardcoded 1.4/0.6
          if (scale > 1.0)
            scale = 1.0;
          double smin = g_nodetm_min / 100.0;
          if (scale < smin)
            scale = smin;
          int dur = soft - starttime; // current optimum duration (ms)
          if (dur < 0)
            dur = 0;
          soft = starttime + (int)(dur * scale);
        }

        // T-01 TMStability (default OFF): best move stabile da piu' iterazioni
        // -> confidenza -> riduci la durata optimum verso il pavimento.
        // REDUCE-ONLY (scale <= 1.0), si compone col NodeTM. Segnale diverso
        // dal NodeTM (persistenza tra iterazioni vs quota-nodi di questa
        // iterazione). Solo riduce, mai spinge soft oltre stoptime.
        if (g_tm_stability && stable_iters > g_tm_stab_thresh) {
          int over = stable_iters - g_tm_stab_thresh;
          double sscale = 1.0 - over * (g_tm_stab_step / 1000.0);
          double sfloor = g_tm_stab_floor / 100.0;
          if (sscale < sfloor)
            sscale = sfloor;
          int dur = soft - starttime;
          if (dur < 0)
            dur = 0;
          soft = starttime + (int)(dur * sscale);
        }

      } // fine path TM v1 (additivo)

      // A4 FIX 2026-07-25: "go movetime N" e' un tempo FISSO ordinato dalla GUI,
      // non una stima da correggere. I fattori qui sopra lo accorciavano (il
      // NodeTM e' reduce-only e scende fino a g_nodetm_min/100 = 58%): analisi e
      // GUI a tempo fisso ricevevano il 58% del tempo chiesto. Nessun effetto sui
      // gate ne' sul gioco a tempo+incremento, dove movetime == -1.
      if (movetime != -1)
        soft = soft_time_limit;

      if (soft > stoptime)
        soft = stoptime; // never exceed the hard cap

      prev_best_move = td.best_move;
      prev_completed_score = score;

      // TM v2 Q-08: stop immediati, indipendenti dal soft-time.
      // (a) single-reply: unica mossa legale a root -> la prima iterazione
      //     completata basta (pensare non cambia la mossa).
      // (b) mate-stop: score di matto per N iterazioni consecutive -> il matto
      //     e' confermato, il tempo extra non serve piu' (robusto ai matti
      //     instabili proprio perche' chiede N conferme, alla Caissa).
      // FIX 2026-07-26: tolto `&& g_tmv2_tc_ok` anche qui, stessa ragione del
      // single-reply a :8258. Un matto confermato per N iterazioni consecutive
      // resta confermato che ci sia o no l'incremento: il tempo extra non serve.
      // Non e' allocazione, e' uno stop -> puo' solo risparmiare tempo.
      if (g_tmv2_mate_stop) {
        // FIX 12/08/2026 — era `|score| >= mate_score`, quindi il contatore
        // scattava anche quando il matto e' CONTRO di noi. L'intento dichiarato
        // dodici righe sopra e' l'opposto e vale solo in un verso: "hai trovato
        // il matto, giocalo invece di continuare a pensare" (SF perse partite
        // CCC proprio per quello).
        // Subire matto e' il caso opposto: fermarsi presto non guadagna nulla e
        // toglie l'unica risorsa che resta in una posizione persa — continuare a
        // cercare la linea che allunga di piu', o scoprire che il matto era un
        // artefatto della potatura. Uno score di matto alla radice NON e' una
        // prova formale: con riduzioni e potature puo' essere instabile, e le
        // N iterazioni consecutive lo rendono raro, non impossibile.
        // ⚠️ NON misurabile: l'evento e' troppo raro perche' un SPRT lo veda.
        // Preso sulla correttezza, come Rule50Formula. Il bench non si muove —
        // il contatore alimenta solo lo stop temporale e il bench e' a
        // profondita' fissa, quindi 252074 non e' una verifica di QUESTO.
        if (score >= mate_score)
          mate_score_iters++;
        else
          mate_score_iters = 0;
      }
      if ((root_single_reply && td.best_move) ||
          (g_tmv2_mate_stop &&
           mate_score_iters >= g_tmv2_mate_iters)) {
        stop_threads.store(true, std::memory_order_relaxed);
        break;
      }

      // TM v2 Q-07 (Caissa): root-singularity stop. Se la best e' cosi' sopra
      // le alternative da essere "singolare", le iterazioni extra non la
      // cambieranno: verifica economica (depth/2, finestra nulla, best ESCLUSA
      // -> riusa il meccanismo excluded_move della singular extension; nessuno
      // store TT durante l'esclusione, quindi niente inquinamento). Un
      // fail-high della verifica sovrascriverebbe la PV root: salvata e
      // ripristinata.
      if (g_tmv2_rsing && g_tmv2_tc_ok && td.best_move && current_depth >= 9) {
        int sabs = score < 0 ? -score : score;
        int elapsed = get_time_ms() - starttime;
        int opt_dur = soft_time_limit - starttime;
        if (sabs < g_tmv2_rs_scorecap &&
            elapsed > opt_dur * g_tmv2_rs_timefrac / 100 &&
            get_time_ms() < soft) {
          int margin = g_tmv2_rs_margin_base -
                       g_tmv2_rs_margin_slope * (current_depth - 9);
          if (margin < g_tmv2_rs_margin_min)
            margin = g_tmv2_rs_margin_min;
          int target = score - margin;
          int save_pv[max_ply + 8];
          int save_len = td.pv_length[0];
          memcpy(save_pv, td.pv_table[0], sizeof(save_pv));
          nn_root_sync(td); // mirror allineato alla root prima di ri-cercare
          int v = td_negamax(td, target - 1, target, current_depth / 2, false,
                             td.best_move);
          memcpy(td.pv_table[0], save_pv, sizeof(save_pv));
          td.pv_length[0] = save_len;
          if (!stop_threads.load(std::memory_order_relaxed) && v < target) {
            stop_threads.store(true, std::memory_order_relaxed);
            break;
          }
        }
      }

      if (get_time_ms() >= soft) {
        stop_threads.store(true, std::memory_order_relaxed);
        break;
      }
    }
  }
}
