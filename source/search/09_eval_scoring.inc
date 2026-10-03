// search/09_eval_scoring.inc -- Valutazione e punteggio delle mosse.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// EVALUATION
// ============================================================================

// Build the Stockfish piece list (codes + squares) from a thread's board.
// Returns the number of pieces written into pieces[]/squares[].
static inline int nn_build_piece_list(ThreadData &td, int pieces[33],
                                      int squares[33]) {
  int count = 0;
  for (int bb_piece = P; bb_piece <= k; bb_piece++) {
    U64 bb = td.bitboards[bb_piece];
    while (bb) {
      int sq = get_ls1b_index(bb);
      pieces[count] = nn_piece_code[bb_piece];
      squares[count] =
          nnue_squares[sq]; // engine square -> SF square (a1=0..h8=63)
      count++;
      pop_bit(bb, sq);
    }
  }
  return count;
}

// DIAGNOSTIC ("eval" UCI command): static NNUE eval of the CURRENT global board
// (the one parse_fen / "position" populate). Builds the piece list straight
// from the global bitboards and calls the stateless oracle nn_eval (whose value
// is, by the NNUE_VERIFY invariant, identical to the incremental nn_pos_eval =
// Eval::evaluate, i.e. SF's "Final evaluation"). Returns centipawns relative to
// the side to move. Used to cross-check the SFNNv9/3072 port against the
// official Stockfish binary on identical FENs (a correct port agrees up to SF's
// UCI pawn-normalisation constant).
int debug_eval_position() {
  int pieces[33], squares[33];
  int count = 0;
  for (int bb_piece = P; bb_piece <= k; bb_piece++) {
    U64 bb = bitboards[bb_piece]; // GLOBAL board set by parse_fen
    while (bb) {
      int sq = get_ls1b_index(bb);
      pieces[count] = nn_piece_code[bb_piece];
      squares[count] = nnue_squares[sq];
      count++;
      pop_bit(bb, sq);
    }
  }
  return nn_eval(side == white, pieces, squares, count, fifty);
}

// Come debug_eval_position ma restituisce l'uscita GREZZA della rete (psqt + positional,
// pre-nn_scale). E' la grandezza che calcola anche il trainer, quindi e' l'unica
// confrontabile in un cross-check. Vedi il commento su nn_eval in nnue_bridge.cpp.
int debug_eval_position_raw() {
  int pieces[33], squares[33];
  int count = 0;
  for (int bb_piece = P; bb_piece <= k; bb_piece++) {
    U64 bb = bitboards[bb_piece];
    while (bb) {
      int sq = get_ls1b_index(bb);
      pieces[count] = nn_piece_code[bb_piece];
      squares[count] = nnue_squares[sq];
      count++;
      pop_bit(bb, sq);
    }
  }
  int raw = 0;
  nn_eval(side == white, pieces, squares, count, fifty, &raw);
  return raw;
}

// Re-sync the incremental NNUE mirror to the thread's current board (full
// refresh). Called at the root of each iterative-deepening iteration.
static inline void nn_root_sync(ThreadData &td) {
  int pieces[33], squares[33];
  int count = nn_build_piece_list(td, pieces, squares);
  nn_pos_set(td.nnpos, td.side == white, pieces, squares, count, td.fifty);
}

static inline int td_evaluate(ThreadData &td) {
  PROF_GUARD(prof_eval);
  // DIAGNOSTIC: skip the NNUE forward, return a cheap material eval. Used by
  // the "EvalOff" UCI option to isolate evaluation cost in an NPS profile.
  if (g_eval_off) {
    static const int mat[12] = {100, 320, 330, 500, 900, 0,
                                100, 320, 330, 500, 900, 0};
    int s = 0;
    for (int p = 0; p < 6; p++)
      s += mat[p] * count_bits(td.bitboards[p]);
    for (int p = 6; p < 12; p++)
      s -= mat[p] * count_bits(td.bitboards[p]);
    return (td.side == white) ? s : -s;
  }
#ifdef NNUE_VERIFY
  // Verification oracle: the incrementally maintained eval must EXACTLY equal
  // a full from-scratch refresh. Build with /DNNUE_VERIFY to enable.
  int pieces[33], squares[33];
  int count = nn_build_piece_list(td, pieces, squares);
  int full = nn_eval(td.side == white, pieces, squares, count, td.fifty);
  int inc = nn_pos_eval(td.nnpos, td.bitboards, td.occupancies);
  if (full != inc) {
    std::cerr << "NNUE MISMATCH ply=" << td.ply << " full=" << full
              << " inc=" << inc << " hash=0x" << std::hex << td.hash_key
              << std::dec << std::endl;
    std::exit(2);
  }
  return inc;
#else
  // Static-eval cache: hash_key mixed with fifty (see threads.h EvalCacheEntry
  // for why fifty is in the key). A hit skips the NNUE forward pass entirely;
  // safe because nn_pos_do/undo keep the lazy accumulator's dirty chain intact
  // whether or not we call nn_pos_eval here.
  if (g_eval_cache) {
    if (g_evalcache_undamp) {
      // N1 (2026-06-12): chiave SENZA fifty, valore UNDAMPED (rule50-indip.)
      // ri-smorzato col fifty corrente alla lettura — stesso design del
      // TT-eval16. Prima il fifty era nella chiave -> la stessa posizione a
      // fifty diverso era un MISS sistematico (finali shuffle). Costo: ±2cp
      // di arrotondamento sul roundtrip; il valore fresco resta esatto.
      ThreadData::EvalCacheEntry &ce =
          td.eval_cache[td.hash_key & ThreadData::EVAL_CACHE_MASK];
      // EvalCacheOptTag: l'optimism entra DENTRO il valore (nnue_bridge.cpp:153) e cambia
      // a ogni iterazione ⇒ un hit con opt diverso serve una eval calcolata con un altro
      // contempt. Con il tag quell'hit diventa un miss e si ricalcola.
      const int opt_now = g_opt_per_thread ? td.opt[td.side] : (int)g_optimism[td.side];
      if (ce.key == td.hash_key) {
        // EvalCacheOptSplit: l'optimism entra LINEARMENTE nell'eval, quindi invece di
        // buttare l'entry si CORREGGE il delta. `ce.coeff` e' d(eval)/d(optimism) in
        // millesimi (gia' comprensivo di EvalScale), invariante della posizione.
        int e = ce.eval;
        return tt_eval_redamp(e, td.fifty);
      }
      const int v = nn_pos_eval(td.nnpos, td.bitboards, td.occupancies);
      ce.key = td.hash_key;
      ce.eval = tt_eval_undamp(v, td.fifty);
      ce.opt = opt_now;
      ce.coeff = false ? nn_last_opt_coeff(td.nnpos) : 0; // letto solo con OptSplit
      return v;
    }
    const U64 ck = td.hash_key ^ (0x9E3779B97F4A7C15ULL * (U64)(td.fifty + 1));
    ThreadData::EvalCacheEntry &ce =
        td.eval_cache[ck & ThreadData::EVAL_CACHE_MASK];
    const int opt_now = g_opt_per_thread ? td.opt[td.side] : (int)g_optimism[td.side];
    if (ce.key == ck)
      return false ? ce.eval + (opt_now - ce.opt) * ce.coeff / 1000 : ce.eval;
    const int v = nn_pos_eval(td.nnpos, td.bitboards, td.occupancies);
    ce.key = ck;
    ce.eval = v;
    ce.opt = opt_now;
    ce.coeff = false ? nn_last_opt_coeff(td.nnpos) : 0; // letto solo con OptSplit
    return v;
  }
  // Incremental: the accumulator is updated only for the pieces that changed
  // (HalfKAv2_hm king-bucket refresh handled by Stockfish's transform()).
  return nn_pos_eval(td.nnpos, td.bitboards, td.occupancies);
#endif
}

// ============================================================================
// MOVE SCORING
// ============================================================================

// Maximum magnitude of a history score. Kept below the killer-move scores
// (8000) so quiet history can never outrank killers or captures, and bounded
// via "gravity" so the table cannot overflow over a long search.
#define HISTORY_MAX 7000

// Update a history entry with gravity: the value saturates towards
// +/-HISTORY_MAX, giving recent evidence more weight (Stockfish-style).
static inline void td_update_history(int &h, int bonus) {
  PROF_GUARD(prof_hist);
  if (bonus > HISTORY_MAX)
    bonus = HISTORY_MAX;
  else if (bonus < -HISTORY_MAX)
    bonus = -HISTORY_MAX;
  h += bonus - h * (bonus < 0 ? -bonus : bonus) / HISTORY_MAX;
}

// Same gravity update for the int16_t continuation-history entries.
static inline void td_update_history(int16_t &h, int bonus) {
  PROF_GUARD(prof_hist);
  if (bonus > HISTORY_MAX)
    bonus = HISTORY_MAX;
  else if (bonus < -HISTORY_MAX)
    bonus = -HISTORY_MAX;
  int v = (int)h + bonus - (int)h * (bonus < 0 ? -bonus : bonus) / HISTORY_MAX;
  h = (int16_t)v;
}

// Stat bonus per gli update history. Default = depth*depth (originale). Con
// HistBonusSF on usa la forma SF lineare-clampata min(mult*d - sub, max).
static inline int td_stat_bonus(int depth) {
  if (g_hist_bonus_sf) {
    int b = g_hist_bonus_mult * depth - g_hist_bonus_sub;
    if (b > g_hist_bonus_max)
      b = g_hist_bonus_max;
    if (b < 0)
      b = 0;
    return b;
  }
  return depth * depth;
}

// Move-ordering score bands (well separated so the additive quiet histories,
// range about +/-2*HISTORY_MAX, never overlap the "special" move scores):
//   TT move > good captures > killers > counter-move > quiet history > bad
//   captures
#define SCORE_TT_MOVE 2000000
#define SCORE_GOOD_CAPTURE 700000
#define SCORE_KILLER0 600000
#define SCORE_KILLER1 590000
#define SCORE_COUNTER 580000
#define SCORE_BAD_CAPTURE (-700000)

// Pezzo catturato sulla casa target (en passant -> P, coerente con lo scoring).
// Percorso veloce dietro CapturedMailbox: vedi la globale, dichiarata piu' in alto.
// Cronaca di un falso allarme (8/08/2026): sembrava che con CapturedMailbox=1 il
// bench scendesse da 252074 a 191454 (-24%), cioe' un cambio di comportamento e non
// un'ottimizzazione. Era la SONDA a essere sbagliata: i due bench giravano nello
// STESSO processo, e il secondo partiva con TT e storie gia' calde. Il controllo
// senza alcun cambio di opzione riproduce esattamente la stessa coppia di numeri.
// 🔑 Un bench per processo, sempre. E MB_VERIFY (che confronta tutte e 64 le case
// contro i bitboard) su un bench intero non ha trovato nessuna discrepanza.
static inline int td_captured_piece(ThreadData &td, int target) {
  int start = (td.side == white) ? p : P;
  int end = (td.side == white) ? k : K;
  if (g_captured_mailbox) {
    int pc = td.piece_on[target];
    return (pc >= start && pc <= end) ? pc : P;
  }
  for (int pc = start; pc <= end; pc++)
    if (get_bit(td.bitboards[pc], target))
      return pc;
  return P;
}

// Threat-ordering helper: fill td.threat_by_{pawn,minor,rook} = squares
// attacked by enemy pieces grouped by the cheapest attacker's value. Called
// once per node (the caller checks td.threat_key == td.hash_key first). Only
// reached when ThreatOrdering is on, so it costs nothing when the toggle is
// off.
static inline void td_compute_threats(ThreadData &td) {
  PROF_GUARD(prof_thr);
#ifdef TRIUMV_PROFILE
  prof_n_thr_calls++;
#endif
  const int them = td.side ^ 1; // opponent color (white=0 / black=1)
  const int base = them * 6;    // white pieces P..K at 0, black p..k at 6
  const U64 occ = td.occupancies[both];
  U64 byP = 0, byM, byR, bb;

  bb = td.bitboards[base + P]; // enemy pawns
  while (bb) {
    int s = get_ls1b_index(bb);
    pop_bit(bb, s);
    byP |= pawn_attacks[them][s];
  }
  byM = byP;
  bb = td.bitboards[base + N]; // enemy knights
  while (bb) {
    int s = get_ls1b_index(bb);
    pop_bit(bb, s);
    byM |= knight_attacks[s];
  }
  bb = td.bitboards[base + B]; // enemy bishops
  while (bb) {
    int s = get_ls1b_index(bb);
    pop_bit(bb, s);
    byM |= get_bishop_attacks(s, occ);
  }
  byR = byM;
  bb = td.bitboards[base + R]; // enemy rooks
  while (bb) {
    int s = get_ls1b_index(bb);
    pop_bit(bb, s);
    byR |= get_rook_attacks(s, occ);
  }

  // threat_all = byR + regina + re (TUTTE le minacce nemiche) per la
  // ThreatHist. Calcolato sempre qui (cheap); letto solo quando g_threat_hist
  // e' on.
  U64 byAll = byR;
  bb = td.bitboards[base + Q]; // enemy queens
  while (bb) {
    int s = get_ls1b_index(bb);
    pop_bit(bb, s);
    byAll |= get_queen_attacks(s, occ);
  }
  byAll |= king_attacks[get_ls1b_index(td.bitboards[base + K])]; // enemy king

  td.threat_by_pawn = byP;
  td.threat_by_minor = byM;
  td.threat_by_rook = byR;
  td.threat_all = byAll;
  td.threat_key = td.hash_key;
}

// tier-minaccia di una casa (VALUE-AWARE, cheapest-attacker): 0 = safe, 1 =
// attaccata da pawn/knight/ bishop (attaccante economico -> pericoloso per
// pezzi di valore), 2 = solo da rook/queen/king. Usa i tier che gia' calcoliamo
// (threat_by_minor = pawn+minori, threat_all = tutte).
static inline int td_sq_tier(ThreadData &td, int sq) {
  // NPS 25/09/2026: senza salti. threat_by_minor e' un SOTTOINSIEME di threat_all
  // (byP ⊆ byM ⊆ byR ⊆ all, vedi td_compute_threats), quindi
  //   minor -> 1 ; solo all -> 2 ; niente -> 0   ==   2*all - minor.
  const int m = (int)((td.threat_by_minor >> sq) & 1);
  const int a = (int)((td.threat_all >> sq) & 1);
  return 2 * a - m;
}
// ThreatHist REPLACE-TIERED: bucket-minaccia di una mossa = from_tier*3 +
// to_tier (0..8). Ritorna 0 quando ThreatHist e' OFF -> history_moves[0][..] =
// vecchia tabella = byte-identico. Da chiamare col board del nodo CORRENTE (i
// tier = minacce di QUESTO nodo).
static inline int td_hbucket(ThreadData &td, int move) {
  if (!g_threat_hist)
    return 0;
  if (td.threat_key != td.hash_key)
    td_compute_threats(td);
  return td_sq_tier(td, get_move_source(move)) * 3 +
         td_sq_tier(td, get_move_target(move));
}
// CapHistThreat: bucket della capture history = 1 se la casa di ARRIVO e'
// attaccata dal nemico (il pezzo che cattura puo' essere ripreso), 0 altrimenti.
// Binario e non tiered come td_hbucket: qui conta "mi riprendono si/no", non da
// quale pezzo — il "da quale pezzo" e' gia' nello split SEE good/bad capture.
// Ritorna 0 quando il toggle e' OFF -> tabella piatta = byte-identico.
// Da chiamare col board del nodo dove la cattura viene fatta.
static inline int td_cbucket(ThreadData &td, int target) {
  return 0;
  if (td.threat_key != td.hash_key)
    td_compute_threats(td);
  return get_bit(td.threat_all, target) ? 1 : 0;
}

// F-018.11 (EasyCapGate): vero se un NOSTRO pezzo e' attaccato da un pezzo
// nemico di valore INFERIORE (presa "facile": N/B/R/Q da pedone, R/Q da minore,
// Q da torre). Riusa i tier di td_compute_threats (cache su hash_key -> gratis
// se gia' calcolati).
static inline bool td_has_easy_capture(ThreadData &td) {
  if (td.threat_key != td.hash_key)
    td_compute_threats(td);
  const int base = td.side * 6;
  return (td.threat_by_pawn &
          (td.bitboards[base + N] | td.bitboards[base + B] |
           td.bitboards[base + R] | td.bitboards[base + Q])) ||
         (td.threat_by_minor &
          (td.bitboards[base + R] | td.bitboards[base + Q])) ||
         (td.threat_by_rook & td.bitboards[base + Q]);
}

// Check-ordering helper: check_sq[pt] = squares from which a piece of type pt
// would give a DIRECT check to the enemy king (= squares a pt-attacker reaches
// the king square from). Computed once per node (caller checks td.check_key ==
// td.hash_key). Only when CheckOrdering on.
static inline void td_compute_checks(ThreadData &td) {
  const int them = td.side ^ 1;
  const U64 occ = td.occupancies[both];
  int ksq = get_ls1b_index(td.bitboards[them * 6 + K]); // enemy king square
  td.check_sq[0] =
      pawn_attacks[them][ksq]; // pawn (our pawn checks from these squares)
  td.check_sq[1] = knight_attacks[ksq];             // knight
  td.check_sq[2] = get_bishop_attacks(ksq, occ);    // bishop
  td.check_sq[3] = get_rook_attacks(ksq, occ);      // rook
  td.check_sq[4] = td.check_sq[2] | td.check_sq[3]; // queen
  td.check_sq[5] = 0;                               // king cannot give check
  td.check_key = td.hash_key;
}

// Offense-ordering helper (QuietOffense, port Reckless movepick score_quiet).
// Fills td.offense_sq[pt] = SAFE squares (not attacked by any enemy piece) from
// which a piece of type pt would attack a VULNERABLE enemy piece, and
// td.wall_pawns = our king-shield pawns. Computed once per node (caller checks
// td.offense_key == td.hash_key). Only reached when the toggle is on. Uses OUR
// attack tables per-piece (coordinate-safe), NOT Reckless' setwise fills.
// ⚠️ Le locali sono off_* e MAI p/n/b/r/q: quelli sono gli ENUM dei pezzi neri
// (6..11) e una locale che li ombreggia trasforma td.bitboards[p] in una read
// OOB selvaggia (stessa trappola documentata in syzygy.cpp::to_fathom; costato
// un crash 0xC0000005 posizione-dipendente il 2026-07-24).
static inline void td_compute_offense(ThreadData &td) {
  const int side = td.side;
  const int them = side ^ 1;
  const int bt = them * 6;    // enemy pieces P..K at bt..bt+5
  const int bu = side * 6;    // our pieces

  // wall pawns: se il re e' sulla propria traversa di casa, i pedoni adiacenti
  // sono lo scudo -> penalizza spostarli. home row: bianco rank1 (sq 56..63),
  // nero rank8 (sq 0..7). ⬆ SPOSTATI IN CIMA 2026-07-25: sono l'unico termine
  // vivo di questa funzione e non dipendono dalle minacce.
  int myk = get_ls1b_index(td.bitboards[bu + K]);
  const U64 home = (side == white) ? 0xFF00000000000000ULL : 0x00000000000000FFULL;
  td.wall_pawns = get_bit(home, myk)
                      ? (king_attacks[myk] & (td.bitboards[P] | td.bitboards[p]))
                      : 0;
  td.offense_key = td.hash_key;

  // NPS 2026-07-25: le offense_sq costano ~10 loop di attacchi scorrevoli PER
  // NODO, e il loro UNICO consumatore e' `h += 0` (:4762). Con
  // 0 = 0 — che e' il default, perche' il termine offense-squares
  // e' risultato NEGATIVO in isolamento (−9.70 ± 10.41, LOS 3.38% @1290g) —
  // era lavoro interamente buttato a ogni nodo, incluso il td_compute_threats
  // che serviva solo a loro.
  // Azzerare le maschere e' BYTE-IDENTICO: get_bit(0, target) = 0, quindi
  // `h += 0`, che e' esattamente cio' che accadeva prima con bonus = 0.
  // Rimettendo OffenseBonus != 0 si ritorna al percorso completo.
  // Gate 2026-07-25: con questo early-return disattivato il bench resta
  // 205566, identico -> byte-identita' provata empiricamente, non solo per
  // costruzione.
      for (int i = 0; i < 6; i++)
      td.offense_sq[i] = 0;
    return;
  

  if (td.threat_key != td.hash_key)
    td_compute_threats(td); // need threat_all / threat_by_pawn
  const U64 occ = td.occupancies[both];
  const U64 threats = td.threat_all;
  const U64 safe = ~threats;

  const U64 eB = td.bitboards[bt + B];
  const U64 eR = td.bitboards[bt + R];
  const U64 eQ = td.bitboards[bt + Q];
  U64 bb;

  // non_pawn_threats = enemy knight|bishop|rook|queen|king attacks (per la lever)
  U64 nonpawn = 0;
  bb = td.bitboards[bt + N];
  while (bb) { int s = get_ls1b_index(bb); pop_bit(bb, s); nonpawn |= knight_attacks[s]; }
  bb = eB;
  while (bb) { int s = get_ls1b_index(bb); pop_bit(bb, s); nonpawn |= get_bishop_attacks(s, occ); }
  bb = eR;
  while (bb) { int s = get_ls1b_index(bb); pop_bit(bb, s); nonpawn |= get_rook_attacks(s, occ); }
  bb = eQ;
  while (bb) { int s = get_ls1b_index(bb); pop_bit(bb, s); nonpawn |= get_queen_attacks(s, occ); }
  nonpawn |= king_attacks[get_ls1b_index(td.bitboards[bt + K])];

  // --- pawn offense: case sicure da cui un nostro pedone attacca un pezzo nemico
  //     + attacchi di pedone avanzato (lever ranks, difesi solo da pedone) ---
  U64 off_p = 0;
  bb = td.occupancies[them];
  while (bb) { int e = get_ls1b_index(bb); pop_bit(bb, e); off_p |= pawn_attacks[them][e]; }
  off_p &= safe;
  // lever ranks in coord a8=0: bianco = rank 5-6 (sq 16..31), nero = rank 3-4 (sq 32..47)
  const U64 lever = (side == white) ? 0x00000000FFFF0000ULL : 0x0000FFFF00000000ULL;
  off_p |= td.threat_by_pawn & lever & ~nonpawn;

  // --- knight offense: attacca alfiere-sicuro / torre / donna nemici ---
  U64 knight_vuln = (eB & safe) | eR | eQ;
  U64 off_n = 0;
  bb = knight_vuln;
  while (bb) { int e = get_ls1b_index(bb); pop_bit(bb, e); off_n |= knight_attacks[e]; }
  off_n &= safe;

  // --- bishop offense: attacca una torre nemica ---
  U64 off_b = 0;
  bb = eR;
  while (bb) { int e = get_ls1b_index(bb); pop_bit(bb, e); off_b |= get_bishop_attacks(e, occ); }
  off_b &= safe;

  // --- rook offense: colonna del re nemico (case sicure) ---
  int ek = get_ls1b_index(td.bitboards[bt + K]);
  U64 off_r = (0x0101010101010101ULL << (ek & 7)) & safe;

  // --- queen offense: attacca ortogonalmente un alfiere / diagonalmente una torre ---
  U64 off_q = 0;
  bb = eB & safe;
  while (bb) { int e = get_ls1b_index(bb); pop_bit(bb, e); off_q |= get_rook_attacks(e, occ); }
  bb = eR & safe;
  while (bb) { int e = get_ls1b_index(bb); pop_bit(bb, e); off_q |= get_bishop_attacks(e, occ); }
  off_q &= safe;

  td.offense_sq[0] = off_p;
  td.offense_sq[1] = off_n;
  td.offense_sq[2] = off_b;
  td.offense_sq[3] = off_r;
  td.offense_sq[4] = off_q;
  td.offense_sq[5] = 0;
  // wall_pawns e offense_key sono gia' stati impostati in cima alla funzione.
}

// ---- Q-11 NodeCache (port Caissa NodeCache.cpp) -----------------------------
// Rimpiazzo age-based: una entry di una search PRECEDENTE (gen != corrente)
// puo' essere rubata; una della STESSA search no (Caissa: "allocation failed"
// -> nullptr). Chiave piena verificata sempre -> un hit stantio e' impossibile,
// al massimo un miss.
static inline ThreadData::NCEntry *nc_get(ThreadData &td, U64 key) {
  ThreadData::NCEntry *e = &td.node_cache[key & (ThreadData::NC_SIZE - 1)];
  if (e->key == key) { // stessa posizione: RIUSA le statistiche (cross-move!)
    e->gen = td.nc_gen;
    return e;
  }
  if (e->gen != td.nc_gen) { // slot di una search vecchia: rialloca
    e->key = key;
    e->gen = td.nc_gen;
    e->nodes_sum = 0;
    memset(e->mv, 0, sizeof(e->mv));
    memset(e->mv_nodes, 0, sizeof(e->mv_nodes));
    return e;
  }
  return nullptr; // collisione dentro la stessa search: non rubare
}

static inline ThreadData::NCEntry *nc_try(ThreadData &td, U64 key) {
  ThreadData::NCEntry *e = &td.node_cache[key & (ThreadData::NC_SIZE - 1)];
  return (e->key == key) ? e : nullptr;
}

// Dimezza tutti i contatori (anti-overflow su accumulo cross-move,
// Caissa::ScaleDown).
static inline void nc_scale_down(ThreadData::NCEntry *e) {
  e->nodes_sum = 0;
  for (int i = 0; i < ThreadData::NC_MOVES; i++) {
    e->mv_nodes[i] >>= 1;
    e->nodes_sum += e->mv_nodes[i];
  }
}

static inline void nc_add(ThreadData::NCEntry *e, int move, U64 nodes) {
  int free_i = -1, min_i = -1;
  U64 min_n = ~0ULL;
  for (int i = 0; i < ThreadData::NC_MOVES; i++) {
    if (e->mv[i] == move) { // gia' tracciata: accumula
      e->mv_nodes[i] += nodes;
      e->nodes_sum += nodes;
      if (e->mv_nodes[i] > (1ULL << 40))
        nc_scale_down(e);
      return;
    }
    if (e->mv[i] == 0) {
      if (free_i < 0)
        free_i = i;
    } else if (e->mv_nodes[i] < min_n) {
      min_n = e->mv_nodes[i];
      min_i = i;
    }
  }
  // nuova mossa: slot libero, altrimenti rimpiazza la MENO visitata (solo se
  // costa meno di noi)
  int idx =
      (free_i >= 0) ? free_i : ((min_i >= 0 && min_n < nodes) ? min_i : -1);
  if (idx < 0)
    return;
  e->nodes_sum -= e->mv_nodes[idx];
  e->mv[idx] = move;
  e->mv_nodes[idx] = nodes;
  e->nodes_sum += nodes;
}

static inline int td_score_move(ThreadData &td, int move, int tt_move) {
  PROF_GUARD(prof_score);
  if (move == tt_move)
    return SCORE_TT_MOVE;

  int piece = get_move_piece(move);
  int target = get_move_target(move);

  // Le promozioni sono bombe tattiche: vanno ordinate insieme alle catture
  // forti, non confuse con le mosse silenziose lente.
  if (get_move_promoted(move)) {
    return SCORE_GOOD_CAPTURE + 50000;
  }

  if (get_move_capture(move)) {
    int victim = P;
    int start = (td.side == white) ? p : P;
    int end = (td.side == white) ? k : K;
    // Il test di range replica il ciclo sostituito: cercava solo fra i pezzi
    // AVVERSARI, e se non trovava nulla lasciava il default P. Succede all'e.p.,
    // dove la casa d'arrivo e' VUOTA e la vittima sta altrove.
    const int mb_pc = td.piece_on[target];
    if (mb_pc >= start && mb_pc <= end)
      victim = mb_pc;

    int caphist = g_capture_hist
                      ? td.capture_history[piece][target][victim]
                                          [td_cbucket(td, target)] /
                            g_caphist_div // qui era diviso 16
                      : 0;

    // GoodCapTTQuiet (Reckless #1107, default OFF): TT-move quiet = la mossa
    // migliore nota NON e' una cattura -> le catture per essere "good" devono
    // vincere materiale NETTO (SEE >= +1; pari e perdenti scendono tra le bad).
    // Bypassa anche il lazy-shortcut equal-capture qui sotto (QxQ = SEE 0 =
    // bad).

    // Lazy SEE: capturing a piece of equal-or-greater value is good by
    // definition (no SEE needed). Only run SEE for "attacker > victim"
    // captures, which might be losing. Losing captures (SEE < 0) are
    // ordered AFTER all quiets (still searched, just last).
    // F-018.4 GoodCapHistDiv (0=off): soglia good/bad DIPENDENTE dallo score
    // (Obsidian: -score/32) — una cattura con caphist ottima resta "good" anche
    // con SEE leggermente negativo, una con storia pessima deve vincere
    // materiale netto.
    int gc_thresh = g_goodcap_hist_div ? -(mvv_lva[piece][victim] + caphist) /
                                             g_goodcap_hist_div
                                       : 0;
    if (see_piece_values[piece] <= see_piece_values[victim] ||
        td_see_at_least(td, move, gc_thresh))
      return SCORE_GOOD_CAPTURE + mvv_lva[piece][victim] + caphist;
    else
      return SCORE_BAD_CAPTURE + mvv_lva[piece][victim] + caphist;
  }

  // Quiet moves
  if (td.killer_moves[0][td.ply] == move)
    return SCORE_KILLER0;
  if (td.killer_moves[1][td.ply] == move)
    return SCORE_KILLER1;

  int prev = td.move_stack[td.ply];
  if (g_countermove && prev &&
      td.counter_moves[get_move_piece(prev)][get_move_target(prev)] == move)
    return SCORE_COUNTER;

  // Butterfly history (MainHistWeight, default 1; SF=2) + continuation history
  // (ContHistWeight, default 1). I conthist sono sommati e poi pesati, cosi' il
  // co-tune puo' bilanciare conthist vs main/pawn senza toccare il loro uso in
  // LMR.
  int h = g_mainhist_weight *
          td.history_moves[td_hbucket(td, move)][piece][target] / 100;
  // ThreatHistWeight (cablato 2026-07-04): scala extra del termine
  // (threat-indexed) di butterfly history nell'ordering. 100 = identita' ->
  // nessun effetto (baseline). Solo con ThreatHist ON.
  if (g_threat_hist && g_threat_hist_weight != 100)
    h = h * g_threat_hist_weight / 100;
  int ch = 0;
  if (prev)
    ch += td.continuation_history[get_move_piece(prev)][get_move_target(prev)]
                                 [piece][target];
  // Longer-range continuation history: the same quiet scored in the context of
  // the move 2 and 4 plies back. Off by default (ContHistMulti).
  // ContHist36 (#4): continuation history a 3-ply (ply-2) e 6-ply (ply-5),
  // scalata da ContHist36Weight, sommata in ch. Default OFF = byte-identico.
  if (g_conthist36) {
    int ch36 = 0;
    if (td.ply >= 2) {
      int p3 = td.move_stack[td.ply - 2];
      if (p3)
        ch36 += td.cont_hist_3[get_move_piece(p3)][get_move_target(p3)][piece]
                              [target];
    }
    if (td.ply >= 5) {
      int p6 = td.move_stack[td.ply - 5];
      if (p6)
        ch36 += td.cont_hist_6[get_move_piece(p6)][get_move_target(p6)][piece]
                              [target];
    }
    ch += g_conthist36_weight * ch36 / 100;
  }
  h += g_conthist_weight * ch / 100;
  // PawnHistory (SF-style, weight 2x): quiet ordering by pawn structure. Indice
  // ricalcolato dal board corrente (= board del nodo durante lo scoring ->
  // corretto, niente staleness). Gated -> OFF = byte-identico.
  if (g_pawn_hist) {
    int pk = td_corr_index(td) & ThreadData::PAWN_HIST_MASK;
    h += g_pawn_hist_weight * (int)td.pawn_history[pk][piece][target] / 100;
  }
  // Threat-ordering (ThreatOrdering, default OFF -> byte-identico):
  // +scala*valore se il pezzo lascia una casa attaccata da uno INFERIORE,
  // -scala*valore se ci entra. Tier come SF (Q vs minacce-di-torre, R vs
  // minore, minore vs pedone); pedoni/re non hanno un "minore" -> nessun
  // termine. Minacce calcolate 1x/nodo (cache hash_key).
  if (g_threat_ordering) {
    if (td.threat_key != td.hash_key)
      td_compute_threats(td);
    U64 lesser = 0;
    bool has = true;
    switch (piece % 6) { // 0=P 1=N 2=B 3=R 4=Q 5=K
    case 4:
      lesser = td.threat_by_rook;
      break; // queen
    case 3:
      lesser = td.threat_by_minor;
      break; // rook
    case 1:
    case 2:
      lesser = td.threat_by_pawn;
      break; // knight / bishop
    default:
      has = false;
      break; // pawn / king
    }
    if (has) {
      int from = get_move_source(move);
      int term =
          (get_bit(lesser, from) ? 1 : 0) - (get_bit(lesser, target) ? 1 : 0);
      if (term)
        h += g_threat_scale * see_piece_values[piece] * term / 100;
    }
  }
  // (ThreatHist REPLACE: la threat-indicizzazione e' GIA' dentro
  // history_moves[bucket][..] sopra,
  //  via td_hbucket. Niente termine additivo separato.)
  // Check-ordering (CheckOrdering, default OFF -> byte-identico): bonus ai
  // quiet che danno SCACCO DIRETTO e non sono blunder (SEE>=-75), come SF.
  // check_sq calcolate 1x/nodo.
  if (g_check_ordering) {
    if (td.check_key != td.hash_key)
      td_compute_checks(td);
    if (get_bit(td.check_sq[piece % 6], target) &&
        td_see_at_least(td, move, -75))
      h += g_check_bonus;
  }
  // Offense-ordering (QuietOffense, default OFF -> byte-identico; port Reckless):
  // +bonus se la quiet porta il pezzo su una casa-offense (attacca sicuro un
  // pezzo nemico vulnerabile), -malus se muove un pedone-scudo del re. offense_sq
  // e wall_pawns calcolate 1x/nodo (cache offense_key).
  // Low-ply history (#5, default OFF -> byte-identico): segnale per-ply
  // near-root, peso che decade con la profondita' (1+2*ply), come SF.
  if (g_lowply && td.ply < ThreadData::LOW_PLY_MAX) {
    h += g_lowply_weight * td.lowply_history[td.ply][piece][target] /
         (100 * (1 + 2 * td.ply));
  }
  // Q-11 NodeCache (Caissa MoveOrderer.cpp:431-438, default OFF): vicino alla
  // radice, premia la quiet che ha gia' assorbito molti nodi (= linea
  // profonda/critica). La chiave piena e' verificata da nc_try -> nessun
  // rischio di dati stantii, e la banda (max g_nc_bonus ~4096) resta dentro
  // quella delle history quiet (±14000), lontana da killer/counter (580000+).
  if (g_node_cache && td.ply < ThreadData::NC_MAX_PLY) {
    ThreadData::NCEntry *e = nc_try(td, td.hash_key);
    if (e && e->nodes_sum > (U64)g_nc_min_sum) {
      // nc_add riempie sempre il primo slot libero -> gli occupati sono un
      // prefisso denso: al primo mv==0 non c'e' piu' nulla da cercare.
      for (int i = 0; i < ThreadData::NC_MOVES && e->mv[i]; i++) {
        if (e->mv[i] == move) {
          h += (int)((U64)g_nc_bonus * e->mv_nodes[i] / e->nodes_sum);
          break;
        }
      }
    }
  }
  return h;
}


// (PREFETCH ANTICIPATO nell'ordinamento: provato e RIMOSSO il 09/09/2026.
//  Muoveva da un fatto vero: teniamo aperti meno miss in parallelo di Stockfish,
//  4,37 contro 4,89, e ogni nostro accesso alla RAM costa 66,7 cicli contro 47,6.
//  L'idea era andare a prendere in anticipo le righe di history della mossa i+3
//  mentre si segna la i. Costruito con le basi calcolate una volta per nodo.
//  RISULTATO: REGRESSIONE del 2,7%, 5.793,6 +- 24,4 cicli/nodo contro 5.642,1 +-
//  15,9, e il parallelismo non si e' mosso di un centesimo (4,34 contro 4,35).
//  🔑 L'errore di ragionamento, che vale piu' del codice: la modifica e' stata
//  progettata sul numero AGGREGATO di parallelismo invece che sul dato per sito,
//  che era gia' disponibile e diceva che td_score_move causa 0,125 accessi alla
//  RAM per nodo. Le mosse di uno stesso nodo condividono il blocco da 1.536 byte
//  di [pezzo_precedente][casa_precedente]: dopo le prime letture e' tutto in
//  cache, e non c'era niente da anticipare. I prefetch sono rimasti come puro
//  costo, 27 istruzioni per nodo.)

// ---- NPS 25/09/2026: scoring delle QUIET in blocco ---------------------------------
// td_score_move rifaceva per OGNI quiet lavoro che vale per tutto il nodo: le righe di
// continuation history della mossa precedente (e a 3/6 ply), la riga di pawn history, la
// low-ply, i test sulle chiavi di minacce/scacchi/offense, la ricerca nella NodeCache.
// Qui si calcola tutto una volta per nodo (QuietCtx) e per mossa restano le letture.
// 🔑 PUNTEGGI IDENTICI BIT PER BIT: stesse espressioni intere, nello stesso ordine, di
// td_score_move (ogni divisione avviene dove avveniva). Nodi identici.
struct QuietCtx {
  int tt_move, k0, k1, counter;
  const int16_t (*c1)[64];   // continuation_history[prev] oppure nullptr
  const int16_t (*c2)[64];   // cont_hist_2 (solo ContHistMulti)
  const int16_t (*c4)[64];   // cont_hist_4 (solo ContHistMulti)
  const int16_t (*c3)[64];   // cont_hist_3 (ContHist36)
  const int16_t (*c6)[64];   // cont_hist_6 (ContHist36)
  const int16_t (*ph)[64];   // pawn_history[pk]
  const int (*lp)[64];       // lowply_history[ply] oppure nullptr
  int lp_div;
  ThreadData::NCEntry *nc;   // NodeCache del nodo (gia' filtrata per nodes_sum)
};

static inline void td_quiet_ctx_init(ThreadData &td, QuietCtx &q, int tt_move) {
  q.tt_move = tt_move;
  q.k0 = td.killer_moves[0][td.ply];
  q.k1 = td.killer_moves[1][td.ply];
  const int prev = td.move_stack[td.ply];
  q.counter = (g_countermove && prev)
                  ? td.counter_moves[get_move_piece(prev)][get_move_target(prev)]
                  : 0;
  q.c1 = prev ? td.continuation_history[get_move_piece(prev)][get_move_target(prev)]
              : nullptr;
  q.c2 = q.c4 = q.c3 = q.c6 = nullptr;
  if (g_conthist36) {
    if (td.ply >= 2) {
      const int p3 = td.move_stack[td.ply - 2];
      if (p3) q.c3 = td.cont_hist_3[get_move_piece(p3)][get_move_target(p3)];
    }
    if (td.ply >= 5) {
      const int p6 = td.move_stack[td.ply - 5];
      if (p6) q.c6 = td.cont_hist_6[get_move_piece(p6)][get_move_target(p6)];
    }
  }
  q.ph = g_pawn_hist ? td.pawn_history[td_corr_index(td) & ThreadData::PAWN_HIST_MASK]
                     : nullptr;
  if (g_lowply && td.ply < ThreadData::LOW_PLY_MAX) {
    q.lp = td.lowply_history[td.ply];
    q.lp_div = 100 * (1 + 2 * td.ply);
  } else {
    q.lp = nullptr;
    q.lp_div = 1;
  }
  if ((g_threat_hist || g_threat_ordering) && td.threat_key != td.hash_key)
    td_compute_threats(td);
  if (g_check_ordering && td.check_key != td.hash_key)
    td_compute_checks(td);
  q.nc = nullptr;
  if (g_node_cache && td.ply < ThreadData::NC_MAX_PLY) {
    ThreadData::NCEntry *e = nc_try(td, td.hash_key);
    if (e && e->nodes_sum > (U64)g_nc_min_sum)
      q.nc = e;
  }
}

static inline int td_score_quiet(ThreadData &td, const QuietCtx &q, int move) {
  if (move == q.tt_move)
    return SCORE_TT_MOVE;
  const int piece = get_move_piece(move);
  const int target = get_move_target(move);
  if (get_move_promoted(move))
    return SCORE_GOOD_CAPTURE + 50000;
  if (get_move_capture(move))
    return td_score_move(td, move, q.tt_move); // mai nello stadio quiet: via lunga, identica
  if (q.k0 == move)
    return SCORE_KILLER0;
  if (q.k1 == move)
    return SCORE_KILLER1;
  if (q.counter && q.counter == move)
    return SCORE_COUNTER;

  int h = g_mainhist_weight *
          td.history_moves[td_hbucket(td, move)][piece][target] / 100;
  if (g_threat_hist && g_threat_hist_weight != 100)
    h = h * g_threat_hist_weight / 100;
  int ch = 0;
  if (q.c1)
    ch += q.c1[piece][target];
  if (q.c2)
    ch += q.c2[piece][target];
  if (q.c4)
    ch += q.c4[piece][target];
  if (g_conthist36) {
    int ch36 = 0;
    if (q.c3)
      ch36 += q.c3[piece][target];
    if (q.c6)
      ch36 += q.c6[piece][target];
    ch += g_conthist36_weight * ch36 / 100;
  }
  h += g_conthist_weight * ch / 100;
  if (q.ph)
    h += g_pawn_hist_weight * (int)q.ph[piece][target] / 100;
  if (g_threat_ordering) {
    U64 lesser = 0;
    bool has = true;
    switch (piece % 6) {
    case 4: lesser = td.threat_by_rook; break;
    case 3: lesser = td.threat_by_minor; break;
    case 1:
    case 2: lesser = td.threat_by_pawn; break;
    default: has = false; break;
    }
    if (has) {
      const int from = get_move_source(move);
      const int term =
          (get_bit(lesser, from) ? 1 : 0) - (get_bit(lesser, target) ? 1 : 0);
      if (term)
        h += g_threat_scale * see_piece_values[piece] * term / 100;
    }
  }
  if (g_check_ordering) {
    if (get_bit(td.check_sq[piece % 6], target) && td_see_at_least(td, move, -75))
      h += g_check_bonus;
  }
  if (q.lp)
    h += g_lowply_weight * q.lp[piece][target] / q.lp_div;
  if (q.nc) {
    const ThreadData::NCEntry *e = q.nc;
    for (int i = 0; i < ThreadData::NC_MOVES && e->mv[i]; i++) {
      if (e->mv[i] == move) {
        h += (int)((U64)g_nc_bonus * e->mv_nodes[i] / e->nodes_sum);
        break;
      }
    }
  }
  return h;
}

static inline void td_sort_moves(ThreadData &td, moves *move_list,
                                 int tt_move) {
  int scores[256];

  for (int i = 0; i < move_list->count; i++) {
    // Punteggio base (TT, catture, history, killer)
    scores[i] = td_score_move(td, move_list->moves[i], tt_move);
  }

  // --- Ordinamento standard (Selection Sort) ---
  for (int i = 0; i < move_list->count - 1; i++) {
    int best = i;
    for (int j = i + 1; j < move_list->count; j++) {
      if (scores[j] > scores[best])
        best = j;
    }
    if (best != i) {
      int tmp_move = move_list->moves[i];
      move_list->moves[i] = move_list->moves[best];
      move_list->moves[best] = tmp_move;

      int tmp_score = scores[i];
      scores[i] = scores[best];
      scores[best] = tmp_score;
    }
  }
}

// Calcola solo gli score ma NON ordina (la selezione e' pick-next).
static inline void td_score_all_moves(ThreadData &td, moves *move_list,
                                      int tt_move, int *scores) {
  for (int i = 0; i < move_list->count; i++)
    scores[i] = td_score_move(td, move_list->moves[i], tt_move);
}
