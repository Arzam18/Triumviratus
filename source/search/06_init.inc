// search/06_init.inc -- Tabelle LMR, UndoInfo, inizializzazione dei thread.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// LMR TABLE
// ============================================================================

// F-008: helper SEE-a-soglia (toggle g_see_ge/g_see_ge_verify definiti sopra,
// prima di set_search_param). In verify si gioca col CLASSICO (invariato).
static inline int td_see_at_least(ThreadData &td, int move, int thr) {
  PROF_GUARD(prof_see);
#ifdef TRIUMV_PROFILE
  prof_n_see++;
#endif
  if (g_see_ge_verify) {
    const int ge = td_see_ge(td, move, thr);
    const int cl = (td_see(td, move) >= thr) ? 1 : 0;
    ++g_seege_checks;
    if (ge != cl) {
      const long long b = ++g_seege_bad;
      std::lock_guard<std::mutex> lk(output_mutex);
      printf("info string SeeGE MISMATCH #%lld move=%d thr=%d ge=%d classic=%d "
             "(checks=%lld)\n",
             b, move, thr, ge, cl, (long long)g_seege_checks);
      fflush(stdout);
    }
    return cl;
  }
  return g_see_ge ? td_see_ge(td, move, thr)
                  : ((td_see(td, move) >= thr) ? 1 : 0);
}

int lmr_table[64][64];
// LMRSFBase (studio finali 25/09/2026, docs/audit_7.1/H_FINALI.md). Nei nodi non-PV a
// profondita' >= 12 Stockfish riduce in media 6,5 ply, noi 3,6, e sopra profondita' 20 il nostro
// albero cresce ~1,25x per ply contro 1,15x. La base della nostra riduzione (lmr_table) ha tre
// differenze dalla loro: pendenza in profondita' meta' (div 4,47 contro ~2,1), troncamento
// all'intero, e indice mosse da 0 (la 2a mossa ha ln(1) = 0; SF usa moveCount da 1).
// Con LMRSFBase=1 la LMRFine parte dalla base di SF 19 (search.cpp:713, :1886):
//   R(i) = int(LMRSFMult/100 * ln i),  base = R(depth) * R(mossa, da 1) + LMRSFOff   [1/1024 ply]
// Tutti gli altri termini della LMRFine restano i nostri. 0 = off, byte-identico.
int lmr_sf_table[64][64];
int lmr_deep_table[64][64]; // LMRDeepK, 1/1024 ply

void init_lmr_table() {
  for (int depth = 0; depth < 64; depth++) {
    for (int moves = 0; moves < 64; moves++) {
      if (depth == 0 || moves == 0) {
        lmr_table[depth][moves] = 0;
      } else {
        lmr_table[depth][moves] =
            (int)(g_lmr_base_x100 / 100.0 +
                  log(depth) * log(moves) / (g_lmr_div_x100 / 100.0));
      }
    }
  }
  // LMRSFBase: tabella alla SF, indice mosse = moves_searched + 1 (numero della mossa)
  for (int depth = 0; depth < 64; depth++)
    for (int moves = 0; moves < 64; moves++) {
      const int rd = depth > 0 ? (int)(g_lmr_sf_mult / 100.0 * log(depth)) : 0;
      const int rm = (int)(g_lmr_sf_mult / 100.0 * log(moves + 1));
      lmr_sf_table[depth][moves] = depth > 0 ? rd * rm + g_lmr_sf_off : 0;
      // LMRDeepK: solo sopra d0, indice mosse = numero della mossa (moves_searched + 1)
      lmr_deep_table[depth][moves] =
          depth > g_lmr_deep_d0
              ? (int)(g_lmr_deep_k * (log(depth) - log(g_lmr_deep_d0)) * log(moves + 1))
              : 0;
    }
}

// LMP thresholds
const int lmp_table[9] = {0, 3, 5, 8, 13, 20, 30, 42, 56};

// ============================================================================
// UNDO STRUCTURE
// ============================================================================

struct UndoInfo {
  int captured_piece;
  int captured_square;
  int old_castle;
  int old_enpassant;
  int old_fifty;
  U64 old_hash;
};

// ============================================================================
// THREAD INITIALIZATION
// ============================================================================

// Q-26 HistPrior (Caissa): dopo un azzeramento, riempi le history col PRIOR
// positivo (0 = zero-init storico). Da chiamare OVUNQUE si azzerino le history,
// cosi' il prior non dipende dal punto di reset (init_threads / ucinewgame /
// bench).
void apply_history_priors(ThreadData &td) {
  if (g_hist_init_quiet) {
    for (auto &bucket : td.history_moves)
      for (auto &piece : bucket)
        for (int &v : piece)
          v = g_hist_init_quiet;
  }
  if (g_hist_init_capt) {
    for (auto &piece : td.capture_history)
      for (auto &sq : piece)
        for (auto &vic : sq) // CapHistThreat: livello bucket-minaccia
          for (int &v : vic)
            v = g_hist_init_capt;
  }
}

// Q-25 HistAge (Stormphrax 665b76b = 750/1024; Pawnocchio 3/4; Caissa 7/8): la
// history DECADE tra una mossa e l'altra (evidenza vecchia = meno affidabile).
// 1024 = off. Solo history_moves + capture_history: le conthist sono
// 12*64*12*64 int16 = 2.4M entry per tabella, scansionarle a ogni `go`
// costerebbe piu' del guadagno atteso. Q-26 interagisce: il decay tira verso 0,
// il prior sposta il punto di riposo -> co-tunare.
static void age_histories(ThreadData &td) {
  if (g_hist_age >= 1024)
    return;
  for (auto &bucket : td.history_moves)
    for (auto &piece : bucket)
      for (int &v : piece)
        v = (int)((long long)v * g_hist_age / 1024);
  for (auto &piece : td.capture_history)
    for (auto &sq : piece)
      for (auto &vic : sq) // CapHistThreat: livello bucket-minaccia
        for (int &v : vic)
          v = (int)((long long)v * g_hist_age / 1024);
}

void init_threads(int thread_count) {
  if (thread_count < 1)
    thread_count = 1;
  if (thread_count > MAX_THREADS)
    thread_count = MAX_THREADS;

  // Free any existing incremental-NNUE handles before resizing (resize may
  // move/destroy elements); they are recreated fresh in the loop below.
  for (auto &td : thread_data)
    if (td.nnpos) {
      nn_pos_destroy(td.nnpos);
      td.nnpos = nullptr;
    }

  num_threads = thread_count;
  thread_data.resize(num_threads);

  static bool lmr_initialized = false;
  if (!lmr_initialized) {
    init_lmr_table();
    lmr_initialized = true;
  }

  // P1.2: tabelle cuckoo + between per l'upcoming-repetition. Qui Zobrist e
  // magic sono gia' pronti (init_bitboards gira prima in main). Una volta sola.
  static bool cuckoo_initialized = false;
  if (!cuckoo_initialized) {
    init_cuckoo();
    cuckoo_initialized = true;
  }

  for (int i = 0; i < num_threads; i++) {
    thread_data[i].thread_id = i;
    thread_data[i].nnpos = nn_pos_create();
    thread_data[i].nodes = 0;
    thread_data[i].tb_hits = 0;
    thread_data[i].best_move = 0;
    thread_data[i].best_score = -infinity;
    thread_data[i].depth = 0;
    memset(thread_data[i].killer_moves, 0, sizeof(thread_data[i].killer_moves));
    memset(thread_data[i].history_moves, 0,
           sizeof(thread_data[i].history_moves));
    memset(thread_data[i].counter_moves, 0,
           sizeof(thread_data[i].counter_moves));
    memset(thread_data[i].capture_history, 0,
           sizeof(thread_data[i].capture_history));
    memset(thread_data[i].pv_table, 0, sizeof(thread_data[i].pv_table));
    memset(thread_data[i].pv_length, 0, sizeof(thread_data[i].pv_length));
    memset(thread_data[i].eval_stack, 0, sizeof(thread_data[i].eval_stack));
    memset(thread_data[i].reduction_stack, 0,
           sizeof(thread_data[i].reduction_stack));
    // Zero the eval cache once. Entries stay valid across searches (the
    // 64-bit key guards correctness), so we deliberately do NOT clear it
    // per-go — that preserves hits from transpositions across moves.
    memset(thread_data[i].eval_cache, 0, sizeof(thread_data[i].eval_cache));
    memset(thread_data[i].corr_hist, 0, sizeof(thread_data[i].corr_hist));
    memset(thread_data[i].corr_hist_minor, 0,
           sizeof(thread_data[i].corr_hist_minor));
    memset(thread_data[i].corr_hist_major, 0,
           sizeof(thread_data[i].corr_hist_major));
    memset(thread_data[i].corr_hist_material, 0,
           sizeof(thread_data[i].corr_hist_material));
    memset(thread_data[i].corr_hist_trans, 0,
           sizeof(thread_data[i].corr_hist_trans));
    memset(thread_data[i].corr_hist_np, 0, sizeof(thread_data[i].corr_hist_np));
    memset(thread_data[i].cont_corr_hist, 0,
           sizeof(thread_data[i].cont_corr_hist));
    memset(thread_data[i].pawn_history, 0, sizeof(thread_data[i].pawn_history));
    // Q-11 NodeCache: azzerata al boot. NON per-go (le entry sopravvivono
    // cross-move: e' il punto della tecnica). La chiave piena garantisce che
    // un'entry di un'altra posizione non venga mai letta come valida.
    memset(thread_data[i].node_cache, 0, sizeof(thread_data[i].node_cache));
    thread_data[i].nc_gen = 0;
    apply_history_priors(thread_data[i]); // Q-26 (no-op se prior = 0)
  }
}

void copy_board_to_thread(ThreadData &td) {
  memcpy(td.bitboards, bitboards, sizeof(bitboards));
  memcpy(td.occupancies, occupancies, sizeof(occupancies));
  // Mailbox: full init dalla root, poi mantenuta in make/unmake — stesso schema di
  // pawn_key e np_key qui sotto. Unico punto di inizializzazione della board di thread.
  for (int sq = 0; sq < 64; sq++)
    td.piece_on[sq] = -1;
  for (int pc = P; pc <= k; pc++) {
    U64 bb = td.bitboards[pc];
    while (bb) {
      int sq = get_ls1b_index(bb);
      td.piece_on[sq] = pc;
      pop_bit(bb, sq);
    }
  }
  td.side = side;
  td.enpassant = enpassant;
  td.castle = castle;
  td.hash_key = hash_key;
  td.fifty = fifty;
  // P2.1: pawn key incrementale — full init dal board di root, poi mantenuta
  // in make/unmake da td_pawn_key_update (XOR self-inverse, stile OccIncr).
  td.pawn_key = 0;
  for (int pc = P; pc <= p; pc += (p - P)) {
    U64 bb = td.bitboards[pc];
    while (bb) {
      int sq = get_ls1b_index(bb);
      td.pawn_key ^= piece_keys[pc][sq];
      pop_bit(bb, sq);
    }
  }
  // CorrNonPawn: np_key per colore — full init dal board di root, poi mantenute
  // in make/unmake da td_np_key_update (XOR self-inverse).
  td.np_key[white] = td.np_key[black] = 0;
  for (int pc = N; pc <= K; pc++) {
    U64 bb = td.bitboards[pc];
    while (bb) {
      int sq = get_ls1b_index(bb);
      td.np_key[white] ^= piece_keys[pc][sq];
      pop_bit(bb, sq);
    }
  }
  for (int pc = n; pc <= k; pc++) {
    U64 bb = td.bitboards[pc];
    while (bb) {
      int sq = get_ls1b_index(bb);
      td.np_key[black] ^= piece_keys[pc][sq];
      pop_bit(bb, sq);
    }
  }
  // NPS 25/09/2026: chiavi minori/maggiori per la correction history, full init qui
  // e poi incrementali (td_mm_key_update), come pawn_key e np_key.
  td.mm_key[0] = td.mm_key[1] = 0;
  {
    const int mpcs[8] = {N, B, n, b, R, Q, r, q};
    for (int j = 0; j < 8; j++) {
      U64 bb = td.bitboards[mpcs[j]];
      while (bb) {
        int sq = get_ls1b_index(bb);
        td.mm_key[j >= 4] ^= piece_keys[mpcs[j]][sq];
        pop_bit(bb, sq);
      }
    }
  }
  // P2.2: alla radice non ci sono null nel cammino -> finestra limitata solo
  // da fifty/storia. 1024 = "nessuna null vista".
  td.plies_from_null = 1024;
  td.in_nmp_verif = false; // P1.6
  td.seldepth = 0;
  memcpy(td.repetition_table, repetition_table, sizeof(repetition_table));
  td.repetition_index = repetition_index;
  td.ply = 0;
  td.nodes = 0;
  td.best_move = 0;
  td.best_score = -infinity;
  td.depth = 0;
  // Q-11 NodeCache (Caissa::OnNewSearch): nuova generazione. Le entry della
  // search precedente restano LEGGIBILI (statistiche cross-move) ma sono
  // rimpiazzabili. NB: parte da 1 al primo `go` (init a 0), cosi' le entry
  // vergini (gen=0) sono subito allocabili.
  td.nc_gen++;
  // Q-25 HistAge: una ricerca = una mossa giocata -> invecchia la history.
  // No-op a 1024. NB nel bench le history sono azzerate prima di ogni
  // posizione, quindi il decay agisce su tabelle vuote = bench invariato anche
  // con HistAge != 1024.
  age_histories(td);
}
