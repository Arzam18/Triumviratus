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
  // Risposta prevista (TmExpect*): la posizione attesa dopo la nostra mossa e la risposta della linea principale, la
  // nostra mossa successiva prevista, la valutazione e la profondita' della ricerca che le ha date.
  U64    expect_key = 0;
  int    expect_move = 0, expect_score = 0, expect_depth = 0;
  bool   expected = false;          // questa ricerca parte dalla posizione attesa
  bool   surprise = false;          // c'era una risposta prevista e l'avversario ne ha giocata un'altra
  double start_clock = 0;           // orologio alla prima mossa della partita (TmSurpriseClock)
};
static ClockState g_clock;

static void clock_memory_reset() {
  g_clock.opening_adjust = -1;
  g_clock.prev_time_red = 0.85;
  g_clock.have_prev = false;
  g_clock.expect_key = 0;
  g_clock.expected = false;
  g_clock.surprise = false;
  g_clock.start_clock = 0;
}

int clock_hard_limit() { return g_clock.maximum; }

static void clock_plan(int us, int game_ply, int overhead) {
  if (!g_limits.time[us]) {
    g_clock.optimum = g_clock.maximum = INT_MAX / 2;
    return;
  }
  const double clock = g_limits.time[us], inc = g_limits.inc[us];
  if (g_clock.start_clock <= 0)
    g_clock.start_clock = clock;   // prima mossa della partita (TmSurpriseClock)
  int horizon = g_limits.movestogo ? std::min(g_limits.movestogo, 50) : 50;
  if (clock < 1000 && !g_limits.movestogo)
    horizon = (int)(clock * 0.05);
  const double left = std::max(1.0, clock + inc * (horizon - 1) - (double)overhead * (2 + horizon));
  double opt_share, max_mult;
  if (!g_limits.movestogo) {
    if (g_clock.opening_adjust < 0)
      g_clock.opening_adjust = 0.3272 * std::log10(left) - 0.4141;
    // Costanti come parametri Tm* (05/10/2026, per uno SPSA del tempo contro un motore esterno); default = valori di prima.
    const double log_sec = std::log10(std::max(1.0, clock) / 1000.0);
    const double opt_c = std::min(TmOptC / 1e7 + TmOptCLog / 1e8 * log_sec, 0.004905);
    const double max_c = std::max(TmMaxCBase / 1e4 + TmMaxCLog / 1e4 * log_sec, 3.1441);
    opt_share = std::min(TmOptBase / 1e6 + std::pow(game_ply + 3.22713, TmOptPow / 1e5) * opt_c,
                         TmOptClockCap / 1e5 * clock / left) *
                g_clock.opening_adjust;
    max_mult = std::min(TmMaxMultCap / 1e3, max_c + game_ply / (TmMaxPlyDiv / 1e3));   // 6873/1e3 == 6.873
  } else {
    // Idea 8 (04/10/2026): costanti del ramo movestogo come parametri, per uno SPSA dedicato al 40/15 di CCRL. I
    // default riproducono i valori di prima (la divisione IEEE e' arrotondata correttamente: 880/1000.0 == 0.88).
    opt_share = std::min((TmMtgBase / 1000.0 + game_ply / (TmMtgPly / 10.0)) / horizon, TmMtgClock / 1000.0 * clock / left);
    max_mult = TmMtgMaxBase / 1000.0 + TmMtgMaxHorizon / 1000.0 * horizon;
  }
  // TmBehindMul (07/10/2026): con meno orologio dell'avversario si spende meno (vantaggio relativo solo se negativo).
  // Non con movestogo == 1: a fine ciclo l'orologio dell'avversario puo' gia' contenere il tempo del ciclo dopo.
  if (TmBehindMul && g_limits.movestogo != 1) {
    const double mine = g_limits.time[us], theirs = g_limits.time[us ^ 1];
    opt_share *= 1 + TmBehindMul / 1000.0 * std::min((mine - theirs) / (1.0 + mine + theirs), 0.0);
  }
  // TmEarlySave (08/10/2026, audit X3; BAKATA a 200 l'11/10/2026): con >= 24 pezzi gli errori contro SF19 sono rari per entrambi i
  // motori; la quota si riduce e il tempo resta per le fasi 10-23 pezzi, dove in difesa sbagliamo 3-4 volte di piu'.
  // La scacchiera globale e' quella della radice (start_search la copia nel thread prima di chiamare clock_plan).
  if (TmEarlySave && count_bits(occupancies[both]) >= 24)
    opt_share *= 1 - TmEarlySave / 1000.0;
  // Idea F (13/10/2026, utente): tempo per esperto della rete. La quota ottimale si moltiplica per (1 + TmExpK/1000)
  // secondo l'esperto della radice; il massimo della mossa (multiplo dell'ottimo, sotto il tetto dell'orologio) segue.
  // Tutte a 0: tempi identici.
  if (TmExp0 | TmExp1 | TmExp2 | TmExp3) {
    const int e = net_phase(count_bits(occupancies[both]));
    const int k = e == 0 ? TmExp0 : e == 1 ? TmExp1 : e == 2 ? TmExp2 : TmExp3;
    opt_share *= 1 + k / 1000.0;
  }
  g_clock.optimum = (int)std::max(1.0, opt_share * left);
  g_clock.maximum = (int)std::max((double)g_clock.optimum, std::min(TmMaxClock / 1e4 * clock - overhead, max_mult * g_clock.optimum));
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

// AnDirect (Pos Solver, 15/10/2026): le raccolte di posizioni indicano come chiave la vittoria piu' diretta, e chi
// analizza vuole vedere quella; noi (e SF19) scegliamo spesso una mossa tranquilla con un punteggio un po' piu' alto.
// Con due linee esatte entrambe sopra DirectFrom e distanti al massimo DirectGap, se la seconda e' forzante (scacco,
// cattura, promozione) e la prima no, si scambiano. Va chiamata con la scacchiera alla radice.
// Mossa diretta alla radice (AnDirect, AnDirLine): scacco, cattura, promozione, o sacrificio (mossa tranquilla con
// scambio statico negativo). Senza il sacrificio AnDirect perdeva IQ4 162 (chiave Bd5, scambiata con lo scacco Nf5+).
static bool root_direct(ThreadData &td, MoveSafety &s, int m) {
  return is_tactical(m) || checks_before(td, s, m) || !see_holds(td, m, 0);
}

// La prima linea e' tranquilla e nettamente vincente (soglia DirectFrom): le condizioni di AnDirect e AnDirLine.
static bool root_quiet_win(ThreadData &td, MoveSafety &s) {
  const RootLine &a = td.root[0];
  return a.score != -infinity && !a.upper && !a.lower && !val_decided(a.score) &&
         units_to_cp(a.score) >= DirectFrom && !root_direct(td, s, a.pv[0]);
}

// 15/10/2026 sera: restituisce true se ha scambiato; deepen rimette l'ordine prima dell'iterazione successiva, cosi'
// l'albero resta quello di AnShadow e cambiano solo la linea mostrata e la bestmove.
static bool an_direct(ThreadData &td, int lines) {
  if (!(td.an_bits & AN_DIRECT) || lines < 2)
    return false;
  RootLine &a = td.root[0], &b = td.root[1];
  if (a.score == -infinity || b.score == -infinity || a.upper || a.lower || b.upper || b.lower ||
      val_decided(a.score) || val_decided(b.score))
    return false;
  const int ca = units_to_cp(a.score), cb = units_to_cp(b.score);
  if (cb < DirectFrom || ca - cb > DirectGap)
    return false;
  MoveSafety s;
  s.ready = false;
  if (root_direct(td, s, b.pv[0]) && !root_direct(td, s, a.pv[0])) {
    std::swap(a, b);
    return true;
  }
  return false;
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

// ---------------------------------------------------------------------------
// MODALITA' ANALISI (09/10/2026; opzioni e origine delle idee in search/01_params.inc).
// ---------------------------------------------------------------------------

// GoldDigger (AnGold). Meccanismo di ShashChess (amchess), build -DGOLD_DIGGER, letto nel sorgente pubblico: la radice
// viene classificata (stile di Shashin dalla probabilita' di vittoria della valutazione: difesa/"Petrosian" <= 24%,
// equilibrio/"Capablanca" ~50%, attacco/"Tal" 80-89%, vantaggio netto >= 95%) e marcata con tre fatti statici (una
// cattura possibile con un pezzo che vale piu' del catturato, un pedone oltre la meta' scacchiera, un re attaccato con
// peso > 5). Da li': razoring piu' prudente, margine della futility del nodo (la nostra RFP) x1,5 o x1,2, mossa nulla
// saltata quando il calore tattico arriva a 4 (gd_skip_null, 13_search.inc), riduzioni al 97% o 103% e piccoli ritocchi
// per stile, finestra d'aspirazione piu' stretta in equilibrio e piu' larga in attacco. Codice nostro sulla nostra
// ricerca; non portati: fortezze, soglia del numero di mosse per stile, e tutto cio' che ShashChess fa anche senza
// GoldDigger. Statico una volta per ricerca, stile ricalcolato a ogni iterazione col punteggio dell'iterazione prima.
struct GdRoot { bool sacrificial, near_promo, king_danger, many_moves; };

static GdRoot gd_root_facts(ThreadData &td) {
  GdRoot g{};
  for (int i = 0; i < td.root_count; i++) {   // mosse di radice: gia' legali
    const int m = td.root[i].pv[0];
    if (!get_move_capture(m) || kPieceKind[move_piece(m)] == K)
      continue;
    int mover = unit_value[kPieceKind[move_piece(m)]];
    if (get_move_promoted(m))
      mover += unit_value[kPieceKind[get_move_promoted(m) & 15]] - unit_value[P];
    const int vk = get_move_enpassant(m) ? P : kPieceKind[td.piece_on[move_to(m)] & 15];
    if (mover > unit_value[vk]) { g.sacrificial = true; break; }
  }
  // Pedoni bianchi in 5a-7a (righe 1-3 con a8 = 0, case 8-31), neri in 2a-4a (righe 4-6, case 32-55).
  g.near_promo = (td.bitboards[P] & 0x00000000FFFFFF00ULL) || (td.bitboards[p] & 0x00FFFFFF00000000ULL);
  for (int c = white; c <= black && !g.king_danger; c++) {
    U64 a = td_attackers_to(td, get_ls1b_index(td.bitboards[c == white ? K : k]), c ^ 1);
    int sum = 0;
    while (a) {
      const int sq = get_ls1b_index(a);
      const int kind = kPieceKind[td.piece_on[sq] & 15];
      sum += (kind == Q || kind == R) ? 3 : 1;
      pop_lsb_bb(a);
    }
    g.king_danger = sum > 5;
  }
  g.many_moves = td.root_count > 30;
  return g;
}

static void gd_iteration(ThreadData &td, const GdRoot &g, int score) {
  int w, d, l;
  wdl_from_cp(units_to_cp(score), w, d, l);
  const int wp = (w + d / 2 + 5) / 10;   // probabilita' di vittoria in percento
  const bool petrosian = wp <= 24, aggressive = wp >= 80 && wp <= 89, high_tal = wp >= 95;
  const bool strategical = wp >= 45 && wp <= 55 && !g.king_danger && !g.near_promo;
  td.gd_style = aggressive ? 1 : strategical ? 2 : petrosian ? 3 : high_tal ? 4 : 0;
  td.gd_heat = 10 * g.king_danger + 10 * g.sacrificial + 2 * high_tal + 2 * aggressive + 2 * g.near_promo +
               g.many_moves;
  td.gd_rfp_pct = (aggressive || g.sacrificial) ? 150 : strategical ? 120 : 100;
  td.gd_razor = (aggressive || g.sacrificial) ? 150 : strategical ? 50 : 0;
}

// Valori effettivi della modalita' analisi per l'iterazione corrente (dopo lt_update). In partita (an_bits = 0)
// riproducono esattamente il calcolo di prima: deep_w dai parametri DeepKnee/DeepSpan, leve = Deep* x deep_w / 1024.
static void an_iteration(ThreadData &td, bool plain = false) {   // plain: AnQDeep, preset AnDeep 0 per l'iterazione
  const bool on = td.an_bits != 0;
  int deep = on && !plain ? g_an_deep : 0;
  if (on && (td.an_bits & AN_SMP) && (td.thread_id & 1))   // AnSmp: i thread helper dispari col preset piu' largo
    deep = 3;
  const bool ext = on && (td.an_bits & AN_EXT);
  if (deep || ext) {
    const DeepPreset &p = kAnDeep[deep];
    td.deep_w = std::clamp((td.root_depth - kAnDeepKnee) * 1024 / kAnDeepSpan, 0, 1024);
    const int w = td.deep_w;
    td.dz_rfp = p.rfp * w / 1024;
    td.dz_fut = p.fut * w / 1024;
    td.dz_lmr = p.lmr * w / 1024;
    td.dz_nmp = p.nmp * w / 1024;
    td.dz_sing = (ext ? kAnSing : 0) * w / 1024;
    td.dz_ext = (ext ? kAnExt : 0) * w / 1024;
  } else {
    // Idea 2 (Deep*): peso 0..1024 dei termini "profondi", da DeepKnee a DeepKnee + DeepSpan.
    td.deep_w = std::clamp((td.root_depth - DeepKnee) * 1024 / DeepSpan, 0, 1024);
    const int w = td.deep_w;
    td.dz_rfp = DeepRfp * w / 1024;
    td.dz_fut = DeepFut * w / 1024;
    td.dz_lmr = DeepLmr * w / 1024;
    td.dz_nmp = DeepNmp * w / 1024;
    td.dz_sing = DeepSing * w / 1024;
    td.dz_ext = DeepExt * w / 1024;
  }
  if (on && (td.an_bits & AN_NMPV))
    td.lt_nmp_verify_depth = kAnNmpVerify;
  td.hunt_from = (on && (td.an_bits & AN_HUNT)) ? kAnHuntFrom : 16;
  td.crystal_ply = (on && (td.an_bits & AN_CRYSTAL)) ? std::max(2, td.root_depth / 2) : -1;
}

static void deepen(ThreadData &td) {
  const bool main = td.thread_id == 0;
  NodeFrame *root = &td.frames[FRAME_OFFSET];
  prepare_frames(td);
  root->in_check = td_is_square_attacked(td, get_ls1b_index(td.bitboards[td.side == white ? K : k]), td.side ^ 1);
  td.root_side = td.side;   // Contempt: la patta vale meno per chi muove alla radice
  // DrawDefend: dal punteggio con cui si e' chiusa la mossa precedente (fisso per tutta la ricerca e uguale per tutti i
  // thread; ricalcolarlo a ogni iterazione lo spegnerebbe appena la ricerca trova la patta che cercava).
  td.draw_defend = 0;
  if (DrawDefend && g_clock.have_prev && g_clock.prev_best != -infinity && g_clock.prev_best <= -DrawDefendFrom)
    td.draw_defend = (int)((long long)DrawDefend *
                           std::min<long long>(1024, (long long)(-g_clock.prev_best - DrawDefendFrom) * 1024 / DrawDefendSpan) /
                           1024);
  td.deep_w = 0;
  td.lt_lmr_log_mul = -1;   // forza la costruzione della tabella del thread alla prima iterazione (root_depth e' 0)
  lt_update(td);
  // Modalita' analisi: decisa una volta per ricerca (un setoption durante la ricerca vale dalla prossima).
  // Pos Solver (TRIUMV_POS_SOLVER): sempre accesa, qualunque cosa mandi la GUI (utente 14/10/2026).
  const bool analyse = PSV(false, true) || g_analyse_mode || (g_analyse_infinite && g_limits.infinite);
  td.an_bits = analyse ? (g_an_bits | AN_ACTIVE) : 0;
  // Una riga per ricerca, solo dal thread principale e solo in analisi (in partita an_bits = 0: niente stampa), con
  // il preset in uso: chi analizza vede che la modalita' e' accesa e con quali componenti.
  if (main && (td.an_bits & AN_ACTIVE)) {
    static const struct { const char *name; int bit; } kAnNames[] = {
      {"AnExt", AN_EXT}, {"AnNmpVerify", AN_NMPV}, {"AnQsChecks", AN_QSCHK}, {"AnSmp", AN_SMP},
      {"AnCrystal", AN_CRYSTAL}, {"AnHunt", AN_HUNT}, {"AnGold", AN_GOLD},
      {"AnKingDanger", AN_KDANGER}, {"AnCycle", AN_CYCLE},
    {"AnShadow", AN_SHADOW}, {"AnRootAlt", AN_ROOTALT}, {"AnKeepDepth", AN_KEEPDEPTH},
    {"AnLmpNear", AN_LMPNEAR}, {"AnCaptLmr", AN_CAPTLMR}, {"AnNmpZug", AN_NMPZUG}, {"AnStopKeep", AN_STOPKEEP}, {"AnDirect", AN_DIRECT}, {"AnDirLine", AN_DIRLINE}, {"AnShQuiet", AN_SHQUIET}, {"AnQDeep", AN_QDEEP}};
    printf("info string Analysis mode activated AnDeep=%d", g_an_deep);
    for (const auto &o : kAnNames)
      if (td.an_bits & o.bit)
        printf(" %s", o.name);
    printf("\n");
    fflush(stdout);
  }
  an_iteration(td);
  td.gd_heat = -1000;
  td.gd_rfp_pct = 100;
  td.gd_razor = 0;
  td.gd_style = 0;
  const bool gold = td.an_bits & AN_GOLD;
  GdRoot gd_facts{};
  if (gold) {
    gd_facts = gd_root_facts(td);
    gd_iteration(td, gd_facts, static_evaluate(td));
  }

  if (main) {
    for (int &v : g_clock.iter_values)
      v = g_clock.have_prev ? g_clock.prev_best : 0;
    g_clock.iter_idx = 0;
    g_clock.best_changes = 0;
  }
  // AnShadow (Pos Solver, 14/10/2026 notte): si cercano almeno 2 linee a finestra piena, se ne stampano g_multipv.
  const int shown = std::min(g_multipv, td.root_count);
  const int lines = std::min(std::max(g_multipv, (td.an_bits & AN_SHADOW) ? 2 : 1), td.root_count);
  int again = 0;
  double time_red = 1;
  int last_best_depth = 0, last_best_score = -infinity;
  int last_best_pv[max_ply + 4], last_best_len = 0;

  fill_with(&td.low_ply_hist[0][0], (size_t)LOW_PLY_SIZE * QH_SIZE, LowPlyFill);
  for (int c = 0; c < 2; c++)
    for (int i = 0; i < QH_CTX_SIZE; i++)
      td.quiet_hist[c][i] = (int16_t)(td.quiet_hist[c][i] * QuietHistDecay / 1024);

  // SmpSkew (block 5; off at 0): the odd helper threads start SmpSkew plies ahead, so the threads finish their
  // iterations at different times and the shared table receives a wider mix of depths (a known lazy-SMP variant).
  // Thread 0 never changes. Test 17/10/2026, with SmpVoteDepth, 4 threads 2+0.05: +1.72 +- 4.61 over 5,648 games; off,
  // queued for the end of 8.1.
  if (SmpSkew && (td.thread_id & 1))
    td.root_depth = SmpSkew;

  int best = -infinity;
  bool direct_swapped = false;
  int quiet_hist[3] = {-infinity, -infinity, -infinity};   // AnShQuiet: punteggio delle ultime tre iterazioni
  while (td.root_depth + 1 < max_ply && !stop_threads.load(std::memory_order_relaxed) &&
         !(g_limits.depth && main && td.root_depth >= g_limits.depth)) {
    td.root_depth++;
    if (direct_swapped) {   // AnDirect: la ricerca riparte dall'ordine vero delle linee
      std::swap(td.root[0], td.root[1]);
      direct_swapped = false;
    }
    lt_update(td);   // rampa "tempo lungo": valori effettivi delle leve Lt* per questa profondita' di radice
    // Posizione tranquilla e stabile vista dalla ricerca (AnShQuiet, AnQDeep): da QuietFrom, punteggio entro QuietCp per
    // tre iterazioni, prime due mosse non dirette.
    bool quiet_now = false;
    if ((td.an_bits & (AN_SHQUIET | AN_QDEEP)) && td.root_count >= 2 && td.root_depth > QuietFrom &&
        quiet_hist[2] != -infinity && !val_decided(quiet_hist[0]) &&
        std::abs(units_to_cp(quiet_hist[0] - quiet_hist[1])) <= QuietCp &&
        std::abs(units_to_cp(quiet_hist[0] - quiet_hist[2])) <= QuietCp) {
      MoveSafety s;
      s.ready = false;
      quiet_now = !root_direct(td, s, td.root[0].pv[0]) && !root_direct(td, s, td.root[1].pv[0]);
    }
    an_iteration(td, quiet_now && (td.an_bits & AN_QDEEP));   // leve Deep* pesate (deep_w) e i componenti accesi
    if (gold && td.root_depth > 1 && td.root[0].score > -infinity)
      gd_iteration(td, gd_facts, td.root[0].score);
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

    // Varianti di AnShadow (ShadowFrom/ShadowMargin): in questa iterazione si cercano solo le linee mostrate.
    int iter_lines = lines;
    if (lines > shown &&
        (td.root_depth < ShadowFrom ||
         (ShadowMargin && td.root[shown].score != -infinity && td.root[0].score != -infinity &&
          td.root[0].score - td.root[shown].score > ShadowMargin)))
      iter_lines = shown;
    // AnShQuiet: posizione tranquilla e stabile -> una linea sola in questa iterazione.
    if ((td.an_bits & AN_SHQUIET) && quiet_now && iter_lines > shown)
      iter_lines = shown;
    for (td.pv_idx = 0; td.pv_idx < iter_lines; td.pv_idx++) {
      td.pv_last = td.root_count;
      // AnDirLine: con la prima linea tranquilla e vincente, la linea nascosta di AnShadow cerca solo le mosse dirette,
      // portate in testa al resto della lista (ordine relativo invariato).
      if ((td.an_bits & AN_DIRLINE) && td.pv_idx == shown && td.pv_idx >= 1) {
        MoveSafety s;
        s.ready = false;
        if (root_quiet_win(td, s)) {
          RootLine *first = td.root + td.pv_idx, *last = td.root + td.root_count;
          RootLine *mid = std::stable_partition(first, last, [&](const RootLine &rl) { return root_direct(td, s, rl.pv[0]); });
          if (mid != first)   // senza mosse dirette la seconda linea resta quella normale
            td.pv_last = (int)(mid - td.root);
        }
      }
      RootLine &cur = td.root[td.pv_idx];
      memcpy(td.last_pv, cur.prev_pv, sizeof(int) * cur.prev_pv_len);
      td.last_pv_len = cur.prev_pv_len;
      td.sel_depth = 0;

      // Aspiration window (block 5): AspBase + an offset per thread, (id % SmpAspCycle) x SmpAspStep (default id % 8),
      // + the spread of the root scores. The offset makes the threads fail high and low at different moments.
      int delta = AspBase + td.thread_id % SmpAspCycle * SmpAspStep + (int)(std::llabs(cur.mean_sq) / AspMeanSqDiv);
      if (td.gd_style == 2)   // AnGold: a narrower window in balanced positions, a wider one in attack
        delta = std::max(6, delta * 9 / 10);
      else if (td.gd_style == 1 || td.gd_style == 4)
        delta = delta * 9 / 8;
      const int avg = cur.avg_score;
      int alpha = std::max(avg - delta, -infinity);
      int beta = std::min(avg + delta, (int)infinity);
      set_thread_optimism(td, avg);
      nn_root_sync(td);

      int failed_high = 0;
      for (;;) {
        // AspFhRed plies off for each fail high in a row (was 1).
        const int depth = std::max(1, td.root_depth - failed_high * AspFhRed - 3 * (again + 1) / 4 -
                                      (td.pv_idx >= shown ? ShadowRed : 0));   // ShadowRed: a shorter second line
        td.root_delta = beta - alpha;
        best = td.an_bits ? explore<true>(td, root, alpha, beta, depth, false, true)    // analysis mode
                          : explore<false>(td, root, alpha, beta, depth, false, true);  // games: constant terms
        settle_lines(td, td.pv_idx, td.pv_last);
        if (stop_threads.load(std::memory_order_relaxed))
          break;
        U64 total = 0;
        if (main && shown == 1 && td.pv_idx == 0 && (best <= alpha || best >= beta)) {
          for (auto &t : thread_data) total += t.nodes;
          if (total > 10000000)
            report_lines(td, td.root_depth);
        }
        // Outside the window: separate growth after a fail low and a fail high (AspGrowLo, AspGrowHi; one AspGrow served
        // both), and after a fail low beta keeps AspLoKeep/1024 of the window (ours, off; 0 = beta falls to alpha).
        int grow;
        if (best <= alpha) {
          beta = alpha + (int)((long long)(beta - alpha) * AspLoKeep / 1024);
          alpha = std::max(best - delta, -infinity);
          failed_high = 0;
          grow = AspGrowLo;
          if (main)
            g_stop_at_ponderhit.store(false, std::memory_order_relaxed);
        } else if (best >= beta) {
          alpha = std::max(beta - delta, alpha);
          beta = std::min(best + delta, (int)infinity);
          failed_high++;
          grow = AspGrowHi;
        } else
          break;
        delta += grow * delta / 128;
      }

      settle_lines(td, 0, td.pv_idx + 1);
      if (td.pv_idx + 1 == iter_lines && !stop_threads.load(std::memory_order_relaxed))
        direct_swapped = an_direct(td, iter_lines);   // AnDirect before printing: the GUI sees the direct move
      if (main && !stop_threads.load(std::memory_order_relaxed)) {
        U64 total = 0;
        for (auto &t : thread_data) total += t.nodes;
        if (td.pv_idx + 1 == iter_lines || total > 10000000)
          report_lines(td, td.root_depth);
      }
      if (stop_threads.load(std::memory_order_relaxed))
        break;
    }
    // AnStopKeep (14/10/2026 notte, solo con piu' linee): fermata a meta' iterazione, le linee da pv_idx in giu' hanno
    // punteggi di profondita' diverse; quella con un limite superiore fresco (fallimento basso non ancora risolto)
    // torna al valore completo dell'iterazione prima invece di finire dietro a punteggi vecchi delle altre.
    if ((td.an_bits & AN_STOPKEEP) && lines > 1 && stop_threads.load(std::memory_order_relaxed)) {
      for (int i = td.pv_idx; i < td.root_count; i++) {
        RootLine &rl = td.root[i];
        if (rl.upper && rl.prev_score != -infinity && rl.prev_pv_len) {
          rl.score = rl.uci_score = rl.prev_score;
          memcpy(rl.pv, rl.prev_pv, sizeof(int) * rl.prev_pv_len);
          rl.pv_len = rl.prev_pv_len;
          rl.upper = rl.lower = false;
        }
      }
      settle_lines(td, td.pv_idx, td.root_count);
      settle_lines(td, 0, lines);
    }
    td.pv_idx = 0;
    if (!stop_threads.load(std::memory_order_relaxed) && td.root[0].score != -infinity) {   // AnShQuiet
      quiet_hist[2] = quiet_hist[1];
      quiet_hist[1] = quiet_hist[0];
      quiet_hist[0] = td.root[0].score;
    }
    if (!direct_swapped)   // ricerca fermata a meta' iterazione: la regola vale anche per la linea ristampata
      direct_swapped = an_direct(td, iter_lines);

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

    // V0 (08/10/2026): a ricerca fermata a meta' iterazione le linee sono state riordinate con i risultati parziali
    // (solo mosse cercate per intero: root_record non gira dopo uno stop), ma la linea si stampava solo a iterazione
    // finita. La bestmove poteva allora non essere la prima mossa dell'ultima pv stampata (fastchess: "Bestmove
    // does not match beginning of last PV", ~1 volta ogni 3 partite). Si ristampa la linea definitiva, dopo il
    // recupero del matto e della linea perduta.
    // 14/10/2026 notte: anche con MultiPV >= 2 (prima solo con una linea): la bestmove poteva non essere la linea 1
    // mostrata (Arasan 36: linea 1 c5b6, bestmove f2g3). In partita MultiPV e' 1: nulla cambia.
    if (main && stopped && td.completed_depth > 0)
      report_lines(td, td.root_depth);

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
      // TmJumpGuard (11/10/2026, spenta, vedi 01_params.inc): un salto in su molto grande rispetto alla mossa prima
      // e' piu' spesso un errore della ricerca che una scoperta; non si taglia il tempo che servirebbe a smentirlo.
      // Soglia in cp stampati (units_to_cp), cosi' corrisponde a quello che si legge nella GUI.
      if (TmJumpGuard && g_clock.have_prev && !val_decided(best) && !val_decided(g_clock.prev_avg) &&
          units_to_cp(best - g_clock.prev_avg) >= TmJumpGuard)
        falling = std::max(falling, 1.0);
      // TmRedLo/TmRedHi (06/10/2026): estremi della rampa come parametri; 496/1e2 == 4.96, 1879/1e2 == 18.79.
      time_red = lerp_clamped(td.root_depth - last_best_depth, TmRedLo / 1e2, TmRedHi / 1e2, 0.639, 1.712, 0.629, 1.544);
      const double reduction = (1.468 + g_clock.prev_time_red) / (2.284 * time_red);
      const double instability = TmInstabBase / 1000.0 + TmInstabMul / 1000.0 * g_clock.best_changes / num_threads;
      const double heavy = lerp_clamped((double)effort, TmEffortLo, TmEffortHi, 0.969, 0.714, 0.693, 0.838);
      double total_time = g_clock.optimum * falling * reduction * instability * heavy;
      if (td.root_count == 1)
        total_time = std::min<double>(TmSingleMs, total_time);
      // Risposta prevista (TmExpect*, vedi ClockState): il tempo si riduce solo finche' valgono tutte e tre le
      // condizioni, ricontrollate a ogni iterazione; se una salta, la mossa riprende il tempo pieno.
      if (g_clock.expected && TmExpectScale < 1000 && td.root_depth >= g_clock.expect_depth - TmExpectDepth &&
          td.root[0].pv[0] == g_clock.expect_move && td.root[0].score >= g_clock.expect_score - TmExpectMargin)
        total_time *= TmExpectScale / 1000.0;
      // Sorpresa (TmSurpriseScale): l'avversario non ha giocato la risposta prevista, piu' tempo a questa mossa, ma
      // solo con l'orologio ancora sopra TmSurpriseClock % di quello di inizio partita.
      if (g_clock.surprise && TmSurpriseScale > 1000 &&
          100.0 * g_limits.time[td.root_side] >= TmSurpriseClock * g_clock.start_clock) {
        // TmSurpriseGrade (0 = spenta, tempi identici): peso del tempo in piu' secondo quanto la risposta ha cambiato
        // il punteggio.
        if (TmSurpriseGrade) {
          const double w = std::clamp(1.0 + (double)(g_clock.expect_score - best) / TmSurpriseGrade, 0.0, 2.0);
          total_time *= 1.0 + (TmSurpriseScale / 1000.0 - 1.0) * w;
        } else
          total_time *= TmSurpriseScale / 1000.0;
      }
      // TmDefendMul (08/10/2026, audit X3; BAKATA a 500 l'11/10/2026): in difesa (radice fra -TmDefendTo e -TmDefendFrom) piu' tempo; il
      // tetto resta il massimo della mossa. Contro SF19 in difesa sbagliamo 2-3 volte piu' di lui.
      if (TmDefendMul && best <= -TmDefendFrom && best > -TmDefendTo)
        total_time *= 1 + TmDefendMul / 1000.0;
      const int elapsed = get_time_ms() - g_limits.start_ms;
#ifdef TRIUMV_TMDEBUG   // diagnosi della gestione del tempo (08/10/2026): i fattori a ogni iterazione completata
      printf("info string tm d %d el %d opt %d max %d fall %.3f red %.3f tr %.3f inst %.3f ch %.2f heavy %.3f "
             "effort %llu exp %d sur %d best %d prev %.0f iter %d tot %.0f\n",
             td.root_depth, elapsed, g_clock.optimum, g_clock.maximum, falling, reduction, time_red, instability,
             g_clock.best_changes, heavy, (unsigned long long)effort, (int)g_clock.expected, (int)g_clock.surprise, best,
             prev_avg, g_clock.iter_values[g_clock.iter_idx], total_time);
      fflush(stdout);
#endif
      if (elapsed > std::min(total_time, (double)g_clock.maximum) ||
          td.root[lines - 1].score >= mating_in(3) || td.root[0].score == mated_at(2)) {
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
