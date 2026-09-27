// search/12_negamax.inc -- Negamax.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// NEGAMAX (Lazy SMP: each thread runs this independently, sharing only the TT)
// ============================================================================

// ---- Correction history ----------------------------------------------------
// Learns the systematic gap between the NNUE static eval and the value the
// search actually returns, bucketed by PAWN STRUCTURE + side to move (the net
// most often misjudges pawn structures). The learned offset is added to the raw
// static eval, so pruning/ordering see a value closer to what the search will
// confirm.
static constexpr int CORR_BITS = 14;
static constexpr int CORR_SIZE = 1 << CORR_BITS; // 16384 buckets
static constexpr int CORR_MASK = CORR_SIZE - 1;
static constexpr int CORR_GRAIN = 256; // stored value is (cp * GRAIN)
// Max correction (cp) and stored-value clamp are now runtime tunables:
// g_corr_cap and (g_corr_cap * CORR_GRAIN). See g_corr_cap / g_corr_lr_div
// above.

// Bucket index from the Zobrist key of a given set of piece types.
static inline int td_corr_index_pieces(ThreadData &td, const int *pcs, int n) {
  U64 k = 0;
  for (int j = 0; j < n; j++) {
    int pc = pcs[j];
    U64 bb = td.bitboards[pc];
    while (bb) {
      int sq = get_ls1b_index(bb);
      k ^= piece_keys[pc][sq];
      pop_bit(bb, sq);
    }
  }
  return (int)(k & CORR_MASK);
}

// Bucket index from the pawn-only Zobrist key (white pawns P, black pawns p).
// P2.1: con PawnKeyIncr (default ON) usa la chiave INCREMENTALE mantenuta in
// make/unmake invece di riscansionare le bitboard a ogni chiamata (corr 1x/nodo
// + pawn_history ~30 quiet/nodo). Stessa chiave per costruzione =
// node-identical.
static inline int td_corr_index(ThreadData &td) {
  if (g_pawn_key_incr)
    return (int)(td.pawn_key & CORR_MASK);
  const int pcs[2] = {P, p};
  return td_corr_index_pieces(td, pcs, 2);
}
// Minor-piece (N/B) and major-piece (R/Q) keyed indices (CorrHistMulti only).
// NPS 25/09/2026: chiavi incrementali (td_mm_key_update). La scansione (td_corr_index_pieces)
// resta come riferimento; l'oracolo di compilazione e' stato tolto nella pulizia del 25/09.
static inline int td_corr_index_minor(ThreadData &td) {
  return (int)(td.mm_key[0] & CORR_MASK);
}
static inline int td_corr_index_major(ThreadData &td) {
  return (int)(td.mm_key[1] & CORR_MASK);
}
// Material-key index (SF #5556): rolling hash dei 12 popcount = firma del
// MATERIALE (conteggi), indipendente dalle posizioni. Mascherato come le altre
// corr. Solo con g_corr_material on.
static inline int td_corr_index_material(ThreadData &td) {
  U64 mk = 0;
  for (int pc = P; pc <= k; pc++)
    mk = mk * 0x9E3779B97F4A7C15ULL + (U64)count_bits(td.bitboards[pc]);
  return (int)(mk & CORR_MASK);
}
// Non-pawn key di UN SOLO colore (re incluso, come SF nonPawnKey) — CorrNonPawn
// only. Default: chiave INCREMENTALE (np_key, make/unmake). NPKeyIncr=false =
// rescan (oracle: nodi identici per costruzione a parita' di CorrNonPawn).
static inline int td_corr_index_np_white(ThreadData &td) {
  if (g_np_key_incr)
    return (int)(td.np_key[white] & CORR_MASK);
  const int pcs[5] = {N, B, R, Q, K};
  return td_corr_index_pieces(td, pcs, 5);
}
static inline int td_corr_index_np_black(ThreadData &td) {
  if (g_np_key_incr)
    return (int)(td.np_key[black] & CORR_MASK);
  const int pcs[5] = {n, b, r, q, k};
  return td_corr_index_pieces(td, pcs, 5);
}

// Continuation-correction bucket for the current node: keyed by the move 2-ply
// back and the move 1-ply back (the path INTO this node). Returns nullptr if
// either is missing (root / null move / ply<2). Only touched when g_corr_cont
// is on. NB: move_stack[td.ply] is the move that led to THIS node (1-ply back),
// so 2-ply back is move_stack[td.ply-1] (same convention as the conthist-multi
// tables).
static inline int16_t *td_cont_corr_bucket(ThreadData &td) {
  if (td.ply < 1)
    return nullptr;
  int m1 = td.move_stack[td.ply];     // 1-ply back (move into this node)
  int m2 = td.move_stack[td.ply - 1]; // 2-ply back
  if (!m1 || !m2)
    return nullptr;
  return &td.cont_corr_hist[get_move_piece(m2)][get_move_target(m2)]
                           [get_move_piece(m1)][get_move_target(m1)];
}

// TransCorr: indice della transizione padre -> nodo. La chiave del padre e' in cima alla
// repetition_table (ogni sito spinge hash_key PRIMA di make, anche la null move). -1 alla
// radice e dopo una null move (move_stack = 0), come la cont_corr: nessuna mossa da correggere.
static inline int td_corr_index_trans(ThreadData &td) {
  if (td.ply < 1 || !td.move_stack[td.ply])
    return -1;
  return (int)((td.hash_key ^ td.repetition_table[td.repetition_index]) & CORR_MASK);
}

// Correction (cp) to add to the raw static eval for this position. Sums the
// pawn table with the minor/major material tables when CorrHistMulti is on,
// then clamps the TOTAL correction to g_corr_cap.
// ⛔ CACHE DEGLI INDICI minor/major: PROVATA E RIGETTATA il 4/08/2026.
// **56/150 posizioni (37,3%), z = 3,02, p = 0,0025** = significativamente NEGATIVA
// (PGO avx2, Hash 256, nodi identici, bench 207259).
//
// L'ipotesi era buona sulla carta: ogni indice costa una scansione di 4 bitboard e
// coi default (CorrHistMulti ON, CorrUncert ON) venivano ricalcolati fino a 6 volte
// per nodo — 2 in td_corr_value, 2 in td_corr_uncert due righe dopo, 2 all'uscita.
// 🔑 Ma le 4 bitboard sono 32 byte SEMPRE IN L1 e il ciclo e' un pop_lsb prevedibile:
// la scansione costa meno del confronto sulla chiave piu' i due campi extra in
// ThreadData, che allargano una struttura gia' calda. E fra un nodo e l'altro la
// hash_key cambia sempre, quindi la prima chiamata di ogni nodo e' comunque un miss.
// Stessa lezione del prefetch PSQT: dove il dato e' gia' vicino, aggiungere un
// meccanismo per raggiungerlo piu' in fretta toglie invece di dare.
// Tolta nella pulizia del 25/09/2026: da quel giorno le chiavi minor/major sono incrementali.
static inline void td_corr_mm(ThreadData &td, int &mi, int &ma) {
  mi = td_corr_index_minor(td);
  ma = g_corr_major ? td_corr_index_major(td) : 0;
}

static inline int td_corr_value(ThreadData &td, int idx) {
  PROF_GUARD(prof_corr);
  int sum = td.corr_hist[td.side][idx];
  if (g_corr_multi) {
    int mi, ma;
    td_corr_mm(td, mi, ma);
    sum += td.corr_hist_minor[td.side][mi];
    if (g_corr_major)
      sum += td.corr_hist_major[td.side][ma];
  }
  if (g_corr_nonpawn) {
    sum += g_corr_np_weight *
           (td.corr_hist_np[0][td.side][td_corr_index_np_white(td)] +
            td.corr_hist_np[1][td.side][td_corr_index_np_black(td)]) /
           100;
  }
  if (g_corr_cont) {
    if (int16_t *cc = td_cont_corr_bucket(td))
      sum += g_corr_cont_weight * (int)(*cc) / 100;
  }
  if (g_corr_material) {
    sum += g_corr_material_weight *
           td.corr_hist_material[td.side][td_corr_index_material(td)] / 100;
  }
  if (g_corr_trans_weight) {
    const int ti = td_corr_index_trans(td);
    if (ti >= 0)
      sum += g_corr_trans_weight * td.corr_hist_trans[td.side][ti] / 100;
  }
  int corr = sum / CORR_GRAIN;
  if (corr > g_corr_cap)
    corr = g_corr_cap;
  if (corr < -g_corr_cap)
    corr = -g_corr_cap;
  return corr;
}

// P1a CorrUncert: disaccordo (somma delle distanze a coppie / 2) tra le tre
// tavole di correzione per QUESTA posizione, in cp, cappato a g_cu_cap.
// Chiamata solo con g_corr_uncert on (paga 2 indici extra minor/major).
static inline int td_corr_uncert(ThreadData &td, int idx) {
  int p = td.corr_hist[td.side][idx];
  int im, ja;
  td_corr_mm(td, im, ja);  // hit: td_corr_value l'ha appena calcolata
  int mi = td.corr_hist_minor[td.side][im];
  // Senza il termine dei maggiori: (d1+d2+d3)/2 con tre valori e' l'intervallo fra
  // il minimo e il massimo; con due valori l'analogo esatto e' |p - mi|.
  if (!g_corr_major) {
    int d = p - mi;
    if (d < 0)
      d = -d;
    int u2 = d / CORR_GRAIN;
    return u2 > g_cu_cap ? g_cu_cap : u2;
  }
  int ma = td.corr_hist_major[td.side][ja];
  int d1 = p - mi;
  if (d1 < 0)
    d1 = -d1;
  int d2 = p - ma;
  if (d2 < 0)
    d2 = -d2;
  int d3 = mi - ma;
  if (d3 < 0)
    d3 = -d3;
  int u = (d1 + d2 + d3) / (2 * CORR_GRAIN);
  return u > g_cu_cap ? g_cu_cap : u;
}

// One bucket's gravity update toward `target`, clamped to +/-lim.
// FIX DEFINITIVO (P0.X): Sostituzione della buggy EMA (che causava
// sottocorrezione cronica) con il gravity update puro in stile Stockfish.
// 'target' e' gia' l'errore residuo moltiplicato per CORR_GRAIN.
static inline void td_corr_bucket_update(int &cv, int target, int w, int lim) {
  int bonus = target * w / g_corr_lr_div;
  // CorrAsym: nudge asimmetrico verso l'alto (se presente)
  if (bonus > 0)
    bonus = bonus * g_corr_asym / 128;

  // Stockfish-style decay: proporzionale al valore corrente e alla magnitudine
  // del bonus
  int abs_bonus = bonus > 0 ? bonus : -bonus;
  // AUDIT D (26/09/2026): prodotto a 64 bit. In int, con |diff| > ~2.300 cp (finali con Syzygy) cv*abs_bonus
  // superava 2^31: overflow con segno (UB, trovato con UBSan) e voce di correzione avvolta in spazzatura.
  // Identico al vecchio calcolo in ogni caso senza overflow (bench invariato).
  int decay = (int)(((long long)cv * abs_bonus) / lim);

  cv += bonus - decay;

  if (cv > lim)
    cv = lim;
  else if (cv < -lim)
    cv = -lim;
}

// Update the bucket(s) toward (best_score - static_eval), gated by the bound
// type so we never learn from a value that contradicts the bound direction.
// PROF: l'update sta in prof_corr insieme alle letture.
static inline void td_corr_update(ThreadData &td, int idx, int static_eval,
                                  int best_score, int bound, int depth,
                                  bool in_check, int best_move,
                                  int excluded_move) {
  PROF_GUARD(prof_corr);
  if (!g_corr_hist || in_check || excluded_move)
    return;
  // CorrFailLowAll: su un fail-low SF non ha best move, quindi aggiorna sempre
  // (e con peso 18 contro 12). Storico: solo se la "migliore delle fallite" e' quiet.
  const bool faillow_all = g_corr_faillow_all && bound == hash_flag_alpha;
  if (!faillow_all) {
    if (best_move == 0)
      return;
    if (get_move_capture(best_move) || get_move_promoted(best_move))
      return; // quiet best only
  }
  // 🔴 FIX 2026-08-02 — la soglia era `mate_score` (30000), ma uno score da tablebase vale
  // `TB_VALUE_WIN - ply` = **29873..29936** (syzygy.cpp:29): la banda TB passava INTERA.
  // Un verdetto Syzygy non e' una valutazione, e' una certezza: entrava in `diff` come se
  // fosse un errore di eval da ~29.900, saturava il bucket a ±CorrCap e ci restava,
  // avvelenando OGNI posizione con la stessa pawn key (14 bit ⇒ anche per collisione).
  // In piu' `decay = cv * abs_bonus / lim` a riga 6249 andava in **overflow di int**
  // (5,5e9 > INT32_MAX ⇒ UB, non semplice wrap).
  // Invisibile in tutti i nostri test, che girano SENZA tablebase — ed e' attivo nelle
  // partite di rating, che usano 6 pezzi. Stesso schema del gate TMv2 a incremento zero.
  // Nessun toggle: e' correttezza, e senza tablebase configurate e' un no-op esatto
  // (nessuno score cade fra 29873 e 30000 se non viene da Syzygy).
  // 🔴 IL FIX DEL 2/08 QUI SOPRA E' UN NO-OP — trovato il 7/08/2026.
  // `mate_score - max_ply` = 29936 = `TB_VALUE_WIN`, cioe' il bordo **ALTO** della
  // banda [29873, 29936] che il commento stesso descrive. La guardia `>= 29936`
  // cattura un solo valore, e quel valore non arriva mai qui:
  //   - il probe e' gated da `td.ply &&` (:7146) ⇒ lo score vale al piu' 29935;
  //   - il nodo che fa il probe RITORNA subito (:7160) senza chiamare td_corr_update;
  //   - quello che arriva al padre e' TB_VALUE_WIN - (ply+1) ≤ 29935.
  // ⇒ la banda passa ancora INTERA e il difetto descritto sopra e' tuttora vivo.
  // Il ragionamento era giusto, la costante e' il bordo opposto.
  //
  // CorrTBGuard=1 usa il bordo BASSO: `mate_score - 2*max_ply` = 29872, appena sotto
  // il minimo della banda, quindi `>= 29872` la cattura tutta. Sicuro: i mate score
  // valgono `mate_value - ply` ∈ [30936, 31000] e restano filtrati comunque; allargare
  // la banda di rifiuto di 64 unita' (~27 cp all'estremo di un range gia' assurdo) non
  // toglie nessun update legittimo.
  //
  // ⚠️ VINCOLO DI MISURA: senza `SyzygyPath` configurato questo fix e' un no-op ESATTO
  // — nessuno score cade in [29873, 29935] se non viene da Syzygy. Un SPRT senza
  // tablebase misurera' **zero per costruzione**. Va misurato con le TB caricate, che
  // e' il regime delle liste di rating (CCRL usa 6 pezzi) ma non quello dei nostri
  // test: e' esattamente il motivo per cui e' sopravvissuto finora.
  // Sonda prima delle partite: contatore di |best_score| ∈ [29873, 29936] con TB
  // attive. Deve essere > 0 oggi e 0 col fix. Prova binaria, zero partite.
  const int corr_max = g_corr_tb_guard ? mate_score - 2 * max_ply   // 29872
                                       : mate_score - max_ply;      // 29936
  if (best_score >= corr_max || best_score <= -corr_max)
    return;
  if (static_eval >= corr_max || static_eval <= -corr_max)
    return;
  int diff = best_score - static_eval;
  if (bound == hash_flag_beta && diff < 0)
    return; // lower bound: only push eval up
  if (bound == hash_flag_alpha && diff > 0)
    return; // upper bound: only push eval down
  int target = diff * CORR_GRAIN;
  int w = (depth < 16) ? depth : 16; // deeper search = more trust
  if (faillow_all && g_corr_faillow_all >= 2)
    w = w * 3 / 2; // SF: 18/128 sui fail-low contro 12/128 con best move
  int lim = g_corr_cap * CORR_GRAIN; // clamp stored value to the cap
  td_corr_bucket_update(td.corr_hist[td.side][idx], target, w, lim);
  if (g_corr_multi) {
    int mi, ma;
    td_corr_mm(td, mi, ma);  // stessa posizione di td_corr_value all'ingresso nodo
    td_corr_bucket_update(td.corr_hist_minor[td.side][mi], target, w, lim);
    if (g_corr_major)
      td_corr_bucket_update(td.corr_hist_major[td.side][ma], target, w, lim);
  }
  if (g_corr_nonpawn) {
    td_corr_bucket_update(
        td.corr_hist_np[0][td.side][td_corr_index_np_white(td)], target, w,
        lim);
    td_corr_bucket_update(
        td.corr_hist_np[1][td.side][td_corr_index_np_black(td)], target, w,
        lim);
  }
  if (g_corr_cont) {
    if (int16_t *cc = td_cont_corr_bucket(td)) {
      int cv = *cc; // gravity update on the int16 bucket
      // BUG FIX 2026-07-16: con CorrCap>=128, lim = cap*256 >= 32768 >
      // INT16_MAX
      // -> il clamp permetteva 32768, che castato a int16 INVERTE il segno
      // della correzione. Cappa lim al range int16 per QUESTA tabella (le altre
      // sono int).
      int lim16 = lim < 32767 ? lim : 32767;
      // FIX 3: Sparsita' della Tabella. La cont_corr ha 589k entry.
      // Per farle imparare qualcosa prima della fine della ricerca,
      // moltiplichiamo il peso dell'update (w) per 2. Cosi' i pattern rari
      // imparano in fretta.
      int w_cont = w * 2;
      td_corr_bucket_update(cv, target, w_cont, lim16);
      *cc = (int16_t)cv;
    }
  }
  if (g_corr_material) {
    td_corr_bucket_update(
        td.corr_hist_material[td.side][td_corr_index_material(td)], target, w,
        lim);
  }
  if (g_corr_trans_weight) {
    const int ti = td_corr_index_trans(td);
    if (ti >= 0)
      td_corr_bucket_update(td.corr_hist_trans[td.side][ti], target, w, lim);
  }
}

// MulticutCorr: applica un bonus ESPLICITO alle stesse tavole che td_corr_update
// aggiorna, saltando il suo calcolo di `diff` e la sua guardia sul bound. Serve al
// sito multicut, dove il segnale non e' `best_score - static_eval` a fine nodo ma
// `s - static_eval` prima ancora che il move loop cominci.
// Il bonus passa comunque per td_corr_bucket_update, quindi asimmetria (CorrAsym),
// decay e clamp restano quelli del motore: qui si cambia QUANDO si impara, non COME.
// Il chiamante passa w = g_corr_lr_div per far transitare `target` come bonus esatto
// (bonus = target * w / g_corr_lr_div), cosi' il clamp a ±lim/4 di SF resta l'unico
// limitatore e resta applicato dove SF lo applica.
static inline void td_corr_bonus(ThreadData &td, int idx, int target, int w) {
  int lim = g_corr_cap * CORR_GRAIN;
  td_corr_bucket_update(td.corr_hist[td.side][idx], target, w, lim);
  if (g_corr_multi) {
    int mi, ma;
    td_corr_mm(td, mi, ma);
    td_corr_bucket_update(td.corr_hist_minor[td.side][mi], target, w, lim);
    if (g_corr_major)
      td_corr_bucket_update(td.corr_hist_major[td.side][ma], target, w, lim);
  }
  if (g_corr_nonpawn) {
    td_corr_bucket_update(
        td.corr_hist_np[0][td.side][td_corr_index_np_white(td)], target, w,
        lim);
    td_corr_bucket_update(
        td.corr_hist_np[1][td.side][td_corr_index_np_black(td)], target, w,
        lim);
  }
  if (g_corr_cont) {
    if (int16_t *cc = td_cont_corr_bucket(td)) {
      int cv = *cc;
      // stesso clamp int16 e stesso raddoppio del peso di td_corr_update: la
      // cont_corr ha 589k entry e senza il x2 non impara in tempo utile.
      int lim16 = lim < 32767 ? lim : 32767;
      td_corr_bucket_update(cv, target, w * 2, lim16);
      *cc = (int16_t)cv;
    }
  }
  if (g_corr_material) {
    td_corr_bucket_update(
        td.corr_hist_material[td.side][td_corr_index_material(td)], target, w,
        lim);
  }
  if (g_corr_trans_weight) {
    const int ti = td_corr_index_trans(td);
    if (ti >= 0)
      td_corr_bucket_update(td.corr_hist_trans[td.side][ti], target, w, lim);
  }
}


// Multi-ply continuation-history update: apply `bonus` to the 2/4-ply
// (ContHistMulti) and 3/6-ply (ContHist36) buckets at a cutoff. No-op unless
// one of those is on. Must be called with td.ply == the current node's ply
// (same context as the 1-ply update).
static inline void td_conthist_multi_update(ThreadData &td, int move,
                                            int bonus) {
  if (!g_conthist_multi && !g_conthist36)
    return;
  int pc = get_move_piece(move), tg = get_move_target(move);
  if (g_conthist_multi) {
    if (td.ply >= 1) {
      int p2 = td.move_stack[td.ply - 1];
      if (p2)
        td_update_history(
            td.cont_hist_2[get_move_piece(p2)][get_move_target(p2)][pc][tg],
            bonus);
    }
    if (td.ply >= 3) {
      int p4 = td.move_stack[td.ply - 3];
      if (p4)
        td_update_history(
            td.cont_hist_4[get_move_piece(p4)][get_move_target(p4)][pc][tg],
            bonus);
    }
  }
  // #4: 3-ply (ply-2) e 6-ply (ply-5). Indipendente da ContHistMulti.
  if (g_conthist36) {
    if (td.ply >= 2) {
      int p3 = td.move_stack[td.ply - 2];
      if (p3)
        td_update_history(
            td.cont_hist_3[get_move_piece(p3)][get_move_target(p3)][pc][tg],
            bonus);
    }
    if (td.ply >= 5) {
      int p6 = td.move_stack[td.ply - 5];
      if (p6)
        td_update_history(
            td.cont_hist_6[get_move_piece(p6)][get_move_target(p6)][pc][tg],
            bonus);
    }
  }
}

// Q-12 CMHC (SF PR #6653): fattore di scala % del bonus CONTHIST per la
// CONSISTENZA della cutoff-move nei contesti della continuation-stack. Piu'
// contesti in cui la mossa ha gia' continuation-history POSITIVA -> update piu'
// grande (rinforza le mosse buone-in-piu'-contesti). Usa gli STESSI ply/tabelle
// che l'update tocca (1-ply sempre, 2/4-ply e 3/6-ply sotto i rispettivi
// toggle). 0 = neutro (fattore 100, byte-identico).
static inline int td_cmhc_factor(ThreadData &td, int move) {
  if (!g_cmhc_scale)
    return 100;
  int pc = get_move_piece(move), tg = get_move_target(move);
  int consistent = 0;
  // 1-ply: con CMHCPly1=1 si legge `move_stack[td.ply]`, la STESSA chiave con cui
  // :8675/:8714 scrive la tabella. Con 0 resta `[td.ply-1]` = comportamento
  // attuale, byte-identico. Nessuna guardia su ply: `move_stack[0]` vale 0 alla
  // radice e il check `p &&` lo scarta, esattamente come fa `prev_cm` a :8675.
  {
    int p = g_cmhc_ply1 ? td.move_stack[td.ply]
                        : (td.ply >= 1 ? td.move_stack[td.ply - 1] : 0);
    if (p &&
        td.continuation_history[get_move_piece(p)][get_move_target(p)][pc][tg] >
            0)
      consistent++;
  }
  if (g_conthist_multi) {
    if (td.ply >= 1) {
      int p = td.move_stack[td.ply - 1];
      if (p &&
          td.cont_hist_2[get_move_piece(p)][get_move_target(p)][pc][tg] > 0)
        consistent++;
    }
    if (td.ply >= 3) {
      int p = td.move_stack[td.ply - 3];
      if (p &&
          td.cont_hist_4[get_move_piece(p)][get_move_target(p)][pc][tg] > 0)
        consistent++;
    }
  }
  if (g_conthist36) {
    if (td.ply >= 2) {
      int p = td.move_stack[td.ply - 2];
      if (p &&
          td.cont_hist_3[get_move_piece(p)][get_move_target(p)][pc][tg] > 0)
        consistent++;
    }
    if (td.ply >= 5) {
      int p = td.move_stack[td.ply - 5];
      if (p &&
          td.cont_hist_6[get_move_piece(p)][get_move_target(p)][pc][tg] > 0)
        consistent++;
    }
  }
  return 100 + g_cmhc_scale * consistent;
}

int td_negamax(ThreadData &td, int alpha, int beta, int depth, bool is_cut_node,
               int excluded_move = 0, bool follow_pv = false) {
  td.pv_length[td.ply] = td.ply;

  // Illegal-position / king-capture guard. If the side-to-move can capture the
  // opponent's king (i.e. the side NOT to move is in check), the position is
  // illegal - typically a malformed input FEN (e.g. a tablebase test position
  // with the enemy king left in check). Searching it would let us actually make
  // the king-capturing move; td_make_move accepts it (our own king is safe),
  // nn_pos_do then removes the enemy king from the NNUE mirror, and the next
  // eval indexes a king-bucket feature for a now-kingless side -> access
  // violation. Return an immediate winning score instead. In legal play the
  // side not to move is never in check, so this costs one attack probe and
  // never fires. (Gate ply==0 provato 2026-06-06: node-identico ma NPS ~0 ->
  // non vale; tenuto pieno. La probe e' troppo cheap per spostare gli NPS.)
  // NPS 25/09/2026: RIMESSO il gate ply==0. Quella misura era a tempo, su un laptop,
  // quando il motore faceva ~7.000 istruzioni/nodo in piu' di oggi; ora si conta con i
  // contatori hardware e ogni test di attacco (5 lookup + branch) e' una frazione
  // visibile. Perche' e' sicuro: ogni nodo con ply > 0 nasce da td_make_move, che
  // RIFIUTA le mosse che lasciano il proprio re attaccato, o da una null move, che si
  // fa solo fuori scacco. In entrambi i casi il lato che NON muove non e' mai sotto
  // scacco. L'unico ingresso non garantito e' la posizione di radice (FEN esterna).
  if (td.ply == 0)
  {
    int opp_king_sq =
        get_ls1b_index((td.side == white) ? td.bitboards[k] : td.bitboards[K]);
    if (td_is_square_attacked(td, opp_king_sq, td.side))
      return mate_value - td.ply;
  }

  // P1.11 draw dither (SF value_draw): ±1cp dal node-count invece di 0 secco,
  // rompe la cecita' da ripetizione fra linee "ugualmente patte".
  if (td.ply && (td_is_repetition(td) || td.fifty >= 100)) {
    // F-013 (2026-07-03): alla 100a semimossa il MATTO prevale sulla patta
    // (FIDE/SF rule50 gate). Ramo freddissimo: paga la verifica "esiste una
    // mossa legale?" solo con fifty>=100 E lato al tratto sotto scacco.
    if (td.fifty >= 100) {
      int ksq50 = get_ls1b_index((td.side == white) ? td.bitboards[K]
                                                    : td.bitboards[k]);
      if (td_is_square_attacked(td, ksq50, td.side ^ 1)) {
        moves ml50[1];
        td_generate_moves(td, ml50, false);
        bool any_legal = false;
        for (int i50 = 0; i50 < ml50->count; i50++) {
          UndoInfo u50;
          td.ply++;
          td.repetition_table[++td.repetition_index] = td.hash_key;
          if (!td_make_move(td, ml50->moves[i50], u50)) {
            td.ply--;
            td.repetition_index--;
            continue;
          }
          td_unmake_move(td, ml50->moves[i50], u50);
          td.ply--;
          td.repetition_index--;
          any_legal = true;
          break;
        }
        if (!any_legal)
          return -mate_value + td.ply; // scacco matto: niente patta-50
      }
    }
    return g_draw_dither ? (1 - (int)(td.nodes & 2)) : 0;
  }

  // P1.4 mate-distance pruning: un matto trovato a questa profondita' non puo'
  // battere uno gia' provato piu' corto -> stringe la finestra e taglia subito.
  // Bound conservativi (mate_value - ply, senza il -1: il check cattura-re
  // sopra puo' restituire un matto a ply+0, vedi return mate_value - td.ply).
  if (g_mate_dist && td.ply) {
    if (alpha < -mate_value + td.ply)
      alpha = -mate_value + td.ply;
    if (beta > mate_value - td.ply)
      beta = mate_value - td.ply;
    if (alpha >= beta)
      return alpha;
  }

  // P1.2 (2026-06-09): ripetizione IMMINENTE (cuckoo). Se esiste una mossa
  // reversibile che riporta in una posizione gia' vista, il lato al tratto puo'
  // forzare la patta senza che la search debba scoprirlo: con alpha sotto la
  // patta, alza alpha a 0 (anti-shuffling, come SF step 2).
  if (g_upcoming_rep && td.ply && alpha < 0 && td.fifty >= 3 &&
      td_upcoming_repetition(td)) {
    alpha = 0;
    if (alpha >= beta)
      return alpha;
  }

  bool pv_node = (beta - alpha > 1);
  int tt_move = 0;
  int tt_score = 0;
  int tt_depth = 0;
  int tt_flag = hash_flag_alpha;
  int tt_eval = tt_eval_none; // P1.1: static eval salvata nell'entry (o none)
  bool tt_pv = false;
  int score;

  // TT probe
  bool tt_hit = probe_tt(td.hash_key, tt_move, tt_score, tt_depth, tt_flag,
                         tt_eval, tt_pv);
  const int tt_score_raw = tt_score; // il blocco di cutoff sotto modifica tt_score sul posto

  // ttPv (SF): un nodo è "PV-ish" se è un vero PV node o se la TT lo ricorda
  // ex-PV. store_pv viene scritto negli store TT (propaga il flag); il segnale
  // NUOVO è ridurre meno la LMR sui nodi non-PV che la TT marca ex-PV. Gated da
  // g_ttpv (OFF = byte-identico).
  bool store_pv = g_ttpv_amount > 0 && (pv_node || (tt_hit && tt_pv));
  td.ttpv_stack[td.ply] = store_pv; // TTPvInherit: letto dal figlio fail-low
  // TTAVAIL (diagnostica, solo con CutoffStats=1): disponibilita' della TT-move
  // per profondita'. Il probe qui sopra e' quello di OGNI nodo della main search.
  if (g_cutoff_stats && td.ply && depth >= 1) {
    const int db = depth > 63 ? 63 : depth;
    td.tt_probe_n[db]++;
    td.tt_probe_hit[db] += tt_hit ? 1 : 0;
    td.tt_probe_mv[db] += tt_move ? 1 : 0;
  }

  // Skip the TT cutoff during a singular search (excluded_move set): we are
  // deliberately re-searching this position without the TT move.
  if (tt_hit && td.ply && !excluded_move) {
    if (tt_depth >= depth && !pv_node) {
      if (tt_score < -mate_score)
        tt_score += td.ply;
      if (tt_score > mate_score)
        tt_score -= td.ply;
      if (g_tt_decisive_clamp)
        tt_score = tt_clamp_decisive(tt_score, td.fifty); // Q-32

      // F-018.3a-c TTCutRefine (default OFF): (a) i fail-high richiedono 1 ply
      // di depth TT in piu'; (b) coerenza bound/tipo-nodo (cutNode ==
      // fail-high); (c) niente cutoff con la patta-50 vicina (lo score TT
      // ignora il fifty).
      bool ttcut_ok = true;
      if (g_ttcut_refine) {
        bool tt_fh = tt_score >= beta;
        // TTCutExact: la clausola (b) deriva tt_fh dallo SCORE contro beta, non dal
        // FLAG. Per un'entry EXACT la "direzione del bound" NON ESISTE: lo score e'
        // il valore VERO del nodo. Ma la clausola la tratta lo stesso, quindi a un
        // cut-node un EXACT con score < beta ha tt_fh=false, la coerenza fallisce e
        // il cutoff viene RIFIUTATO: si ricerca da capo un nodo di cui si conosce
        // gia' il valore esatto a profondita' sufficiente.
        // A differenza di PromoQS e TTEvalNoDecay questo RESTRINGE l'albero: piu'
        // cutoff accettati = meno nodi, nessun costo da ripagare.
        const bool cut_coherent =
            (g_ttcut_exact && tt_flag == hash_flag_exact) || (is_cut_node == tt_fh) ||
            (g_ttcut_depth_override > 0 && depth > g_ttcut_depth_override);
        ttcut_ok = (!tt_fh || tt_depth >= depth + 1) && cut_coherent &&
                   td.fifty < g_ttcut_fifty;
      }

      // P1.13 (SF): il cutoff servito dalla TT non passa dal loop mosse ->
      // senza questo bonus la history del ttMove quiet si raffredda anche se
      // la mossa continua a tagliare. Scala SPSA-tunable (TTCutBonusScale).
      if (g_ttcut_bonus && tt_move && tt_score >= beta &&
          tt_flag != hash_flag_alpha && !get_move_capture(tt_move)) {
        int bonus = td_stat_bonus(depth) * g_ttcut_bonus_scale / 100;
        td_update_history(
            td.history_moves[td_hbucket(td, tt_move)][get_move_piece(tt_move)]
                            [get_move_target(tt_move)],
            bonus);
      }

      if (ttcut_ok) {
        bool would_cut = (tt_flag == hash_flag_exact) ||
                         (tt_flag == hash_flag_alpha && tt_score <= alpha) ||
                         (tt_flag == hash_flag_beta && tt_score >= beta);
        if (would_cut) {
          // F-018.3d TTCutMalus (default OFF, duale di TTCutBonus): la quiet
          // AVVERSARIA che porta a un nodo che cutta subito su TT era una mossa
          // debole -> raffreddala nell'ordering del PADRE (solo se il padre
          // aveva visto poche mosse: il segnale conta all'inizio della lista).
          if (g_ttcut_malus && tt_score >= beta && tt_flag != hash_flag_alpha) {
            int pm = td.move_stack[td.ply];
            if (pm && td.captured_stack[td.ply] == -1 &&
                !get_move_promoted(pm) &&
                td.seen_stack[td.ply - 1] <= g_ttcut_malus_seen) {
              int tmalus = td_stat_bonus(depth);
              td_update_history(
                  td.history_moves[td.hbucket_stack[td.ply]][get_move_piece(pm)]
                                  [get_move_target(pm)],
                  -tmalus);
              if (td.ply >= 2) {
                int pm2 = td.move_stack[td.ply - 1];
                if (pm2)
                  td_update_history(
                      td.continuation_history[get_move_piece(
                          pm2)][get_move_target(pm2)][get_move_piece(pm)]
                                             [get_move_target(pm)],
                      -tmalus);
              }
            }
          }
          // TTDamp (Coda): taglio con bound inferiore -> score spinto verso beta (W in decimi). Non-PV per
          // costruzione di questo blocco; niente score decisivi (matti e TB restano esatti).
          if (g_tt_damp && tt_flag == hash_flag_beta && tt_score >= beta &&
              tt_score < mate_score - 2 * max_ply && tt_score > -(mate_score - 2 * max_ply))
            return (g_tt_damp * tt_score + 10 * beta) / (g_tt_damp + 10);
          return tt_score; // fail-soft
        }
      }
    }

    // TTNearMiss (Coda): entry di UN ply piu' corta, accettata se lo score supera il bound di un margine. Qui
    // tt_depth < depth (altrimenti il blocco sopra ha gia' deciso), quindi tt_score e' ancora quello grezzo.
    if (g_tt_nearmiss && !pv_node && tt_depth == depth - 1 && td.fifty < g_ttcut_fifty &&
        tt_score < mate_score - 2 * max_ply && tt_score > -(mate_score - 2 * max_ply)) {
      if (tt_flag == hash_flag_beta && tt_score - g_tt_nearmiss >= beta)
        return tt_score - g_tt_nearmiss;
      if (tt_flag == hash_flag_alpha && tt_score + g_tt_nearmiss <= alpha)
        return tt_score + g_tt_nearmiss;
    }

    // Q-14 TTResearchMargin (Ethereal): fail-low anticipato a depth-1. Solo
    // entry UPPER a tt_depth>=depth-1, !PV, e ttScore ben sotto alpha
    // (margine). Non si sovrappone al cutoff principale sopra: se
    // tt_depth>=depth e alpha-bound cuttava, il return e' gia' avvenuto. Adjust
    // matto locale (l'entry e' relativa alla root).
    if (g_tt_research && !pv_node && tt_depth >= depth - 1 &&
        tt_flag == hash_flag_alpha) {
      int adj = tt_score;
      if (adj < -mate_score)
        adj += td.ply;
      if (adj > mate_score)
        adj -= td.ply;
      if ((is_cut_node || adj <= alpha) && adj + g_tt_research_margin <= alpha)
        return adj; // fail-soft (adj < alpha garantito dal margine)
    }
  }

  // Time check
  if ((td.nodes & 4095) == 0) {
    if (stop_threads.load(std::memory_order_relaxed))
      return 0;
    // A5: `td_root_move_ready()` = pavimento anti-forfeit, vedi la definizione.
    // Short-circuit: non viene mai valutata finche' il tempo non e' scaduto.
    if (timeset && get_time_ms() > stoptime && td_root_move_ready()) {
      stop_threads.store(true, std::memory_order_relaxed);
      return 0;
    }
  }

  if (depth <= 0) {
    const TtProbeData ttp{tt_hit, tt_move, tt_score_raw, tt_depth, tt_flag, tt_eval, tt_pv};
    return td_quiescence(td, alpha, beta, 0, &ttp);
  }

  // Ply ceiling. Set pv_length here so the parent's PV-copy loop reads a sane
  // (terminating) bound from pv_length[ply+1] instead of OOB garbage.
  if (td.ply >= max_ply) {
    td.pv_length[td.ply] = td.ply;
    return td_evaluate(td);
  }

  td.nodes++;
  if (g_node_limit_thread && td.nodes >= g_node_limit_thread) {
    stop_threads.store(true, std::memory_order_relaxed);
    return 0;
  }
  if (td.ply > td.seldepth)
    td.seldepth = td.ply; // seldepth (info UCI)

  // Syzygy tablebase WDL probe. For a non-root node with no castling rights
  // and few enough pieces, the tablebase gives the exact game value (side-to-
  // move relative), so we short-circuit the search with it. Cursed/blessed
  // results map to 0 (50-move-rule draws); the actual win conversion in the
  // reached position is guaranteed by the root DTZ probe. count_bits runs
  // only when tablebases are loaded (syzygy_max_pieces() > 0).
  {
    unsigned tb_men = syzygy_max_pieces();
    // SyzygyProbeLimit: clamp opzionale sotto TB_LARGEST (default 7 = no-op).
    if (g_syzygy_probe_limit >= 0 && (unsigned)g_syzygy_probe_limit < tb_men)
      tb_men = (unsigned)g_syzygy_probe_limit;
    // S-10 guard fifty==0 (2026-07-09): fathom (tbprobe.h:223) fallisce SEMPRE
    // con rule50!=0 ma pagavamo comunque il marshalling to_fathom -> no-op
    // comportamentale.
    if (tb_men && td.ply && !excluded_move && td.castle == 0 && td.fifty == 0 &&
        depth >= g_syzygy_probe_depth &&
        (unsigned)count_bits(td.occupancies[both]) <= tb_men) {
      int tb_score;
      if (syzygy_probe_wdl(td, td.ply, tb_score)) {
        td.tb_hits++; // -> contatore "tbhits" (Fritz "TBAs")
        // S-10 TBScoreTT (default OFF): le rivisite colpiscono la TT invece
        // di ri-probare. depth+6 come SF (search.cpp step 5).
        if (g_tb_tt_store) {
          int sd = depth + 6;
          if (sd > 254)
            sd = 254;
          store_tt(td.hash_key, 0, tb_score, sd, hash_flag_exact, td.ply);
        }
        return tb_score;
      }
    }
  }

  // NPS 25/09/2026: se il padre ha appena calcolato gives_check per questa mossa, lo
  // scacco al nodo e' gia' noto (vedi chk_hint in threads.h). Stesso valore, un test
  // di attacco in meno per nodo.
  const int chk_hint = td.chk_hint[td.ply];
  bool in_check;
  if (chk_hint >= 0)
    in_check = chk_hint != 0;
  else {
    int king_sq =
        get_ls1b_index((td.side == white) ? td.bitboards[K] : td.bitboards[k]);
    in_check = td_is_square_attacked(td, king_sq, td.side ^ 1);
  }

  // P1.9: gate co-tunabile sulla check-extension (default 128 = sempre =
  // comportamento storico; il co-tune puo' abbassarlo, 0 = mai).
  if (in_check && depth <= g_check_ext_depth)
    depth++;

  // Internal Iterative DEEPENING (gated, default OFF): senza TT move, invece di
  // limitarsi a ridurre la profondita' (IIR economico, sotto), facciamo una
  // mini-ricerca ridotta PRIMA per OTTENERE una hash move buona -> ordinamento
  // ~SF
  // -> il taglio "morde". Solo su pv/cut node (dove l'ordinamento conta), con
  // freno anti-cascata thread_local (niente IID dentro la mini-ricerca:
  // eviterebbe la ricorsione esplosiva). OFF (g_iid=false) = blocco saltato =
  // byte-identico.
  static thread_local bool tl_in_iid = false;
  if (g_iid && !tt_move && !excluded_move && !tl_in_iid &&
      (pv_node || is_cut_node) && depth >= g_iid_depth) {
    int iid_depth = depth - g_iid_reduction;
    if (iid_depth >= 1) {
      tl_in_iid = true;
      td_negamax(td, alpha, beta, iid_depth, is_cut_node, excluded_move);
      tl_in_iid = false;
      // Ri-probe la TT per la mossa appena trovata (solo tt_move; il resto in
      // scratch
      // -> non tocchiamo tt_hit/tt_score/tt_depth/tt_flag: l'IID serve
      // all'ORDINAMENTO).
      int s2 = 0, d2 = 0, f2 = hash_flag_alpha, e2 = tt_eval_none;
      bool p2 = false;
      probe_tt(td.hash_key, tt_move, s2, d2, f2, e2, p2);
    }
  }

  // Internal Iterative Reduction: senza TT move l'ordinamento e' scadente,
  // riduciamo di 1 ply per ottenere a basso costo una hash move. (Se l'IID
  // sopra ha trovato una mossa, tt_move != 0 -> questa riduzione NON scatta.)
  if (depth >= g_iir_min_depth && !tt_move && !excluded_move && !follow_pv &&
      !(g_iir_not_in_check && in_check) &&
      !(g_iir_no_allnode && !pv_node && !is_cut_node))
    depth -= g_iir_amount; // Q-15: niente IIR su follow-PV; fix opz. in-check
  // NB (2026-07-16): l'IIR spara ANCHE in scacco, annullando la check-ext (+1
  // poi -1). Fix "&& !in_check" candidato +1.5 LTC (SF/Alexandria) ma NON
  // confermabile coi tempi attuali (LTC = troppe partite) -> IN CODA al blocco
  // finale, NON bakato a fiducia.

  // cutoffCnt: azzera il contatore 2 ply avanti (come SF (ss+2)=0), cosi e'
  // fresco per i figli di questo nodo. Letto in LMR solo se
  // g_cutoffcnt_penalty>0.
  if (td.ply + 2 < max_ply + 8)
    td.cutoff_cnt[td.ply + 2] = 0;

  // ProbCut sotto scacco (SF step 12, default off): se la TT ricorda una
  // cattura con bound LOWER e score >= beta+margin a profondita' adeguata,
  // taglia subito (~4 Elo SF).
  if (g_probcut_incheck_margin && (in_check || g_probcut_tt_all >= 1) &&
      (!pv_node || g_probcut_tt_all >= 3) && !excluded_move &&
      tt_hit && tt_move && (get_move_capture(tt_move) || g_probcut_tt_all >= 2) &&
      tt_flag == hash_flag_beta && tt_depth >= depth - 4 &&
      tt_score >= beta + g_probcut_incheck_margin && tt_score < mate_score &&
      tt_score > -mate_score && beta < mate_score && beta > -mate_score)
    return beta + g_probcut_incheck_margin;

  // Correction history: bucket for this position (pawn structure + side). Index
  // computed once; reused to apply the correction here and to learn at node
  // exit.
  const int corr_idx = td_corr_index(td);
  int static_eval;
  int corr_val = 0; // correction applied to this node's static eval (used by
                    // CorrValMargin)
  int corr_uncert =
      0; // P1a: disaccordo tra le tavole corr = incertezza locale eval
  int node_raw_eval =
      tt_eval_none; // P1.1: eval PURA (pre-corr) da salvare in TT
  if (g_lazy_eval && in_check) {
    // In check, no forward-pruning rule reads static_eval (all gated
    // !in_check) and corr-update is skipped -> don't pay for the NNUE eval.
    static_eval = 0;
    // FIX P0.6: marcare lo slot come "nessuna eval" invece di 0: i discendenti
    // a ply+2/ply+4 non devono confrontare la loro eval con uno 0 fittizio.
    // (ImprovingFix OFF = vecchio comportamento: slot a 0.)
    td.eval_stack[td.ply] = g_improving_fix ? EVAL_NONE : 0;
  } else {
    // P1.1 TTStaticEval: l'eval salvata in TT evita la forward NNUE (58% del
    // tempo-nodo). In TT vive DE-smorzata dal rule50 -> ri-smorza col fifty
    // CORRENTE (fix staleness). raw resta PURA pre-correction (la corr e'
    // appresa/tempo-variante, si riapplica fresca qui sotto).
    // flag_none = entry eval-only (EvalTTWrite, SF-style): l'UNADJUSTED cachato
    // si ri-finalizza col fifty corrente (nn_finalize) -> ESATTO come
    // td_evaluate su ogni trasposizione. Entry reale: eval de-smorzata ->
    // redamp (path validato +44).
    node_raw_eval =
        (g_tt_static_eval && tt_eval != tt_eval_none)
            ? (tt_flag == hash_flag_none
                   ? nn_finalize(tt_eval, td.fifty,
                                (count_bits(td.occupancies[both]) - 1) / 4)
                                         : tt_eval_redamp(tt_eval, td.fifty))
            : td_evaluate(td);
    static_eval = node_raw_eval;
    if (g_corr_hist) {
      corr_val = td_corr_value(td, corr_idx);
      static_eval += corr_val;
    }
    if (g_corr_uncert && g_corr_hist && g_corr_multi)
      corr_uncert = td_corr_uncert(td, corr_idx);
    td.eval_stack[td.ply] = static_eval;
    // 5.1 (SF :830): su un MISS abbiamo appena pagato la forward NNUE -> cache
    // l'UNADJUSTED (nn_last_unadjusted, fifty-indep) nello slot naturale (no
    // pollution) cosi' i nodi potati non ricalcolano la forward alla rivisita.
    // tt_eval==none qui => td_evaluate() appena chiamato.
    if (g_eval_tt_write && tt_eval == tt_eval_none)
      tt_cache_eval(td.hash_key, nn_last_unadjusted(td.nnpos));
  }

  // P1.1 (2026-06-09): "improved eval" stile SF — se la TT ha uno score il cui
  // bound e' coerente, e' una stima di qualita'-search migliore della static
  // eval: usala per le DECISIONI DI PRUNING
  // (RFP/NMP/razor/futility/probcut/LMR-margin). static_eval resta la base di
  // eval_stack/improving/corr-update (come SF, che tiene ss->staticEval puro e
  // adatta solo `eval`). Toggle TTEvalImprove per A/B.
  int eval = static_eval;
  if (g_tt_eval_improve && !in_check && tt_hit && tt_score > -mate_score &&
      tt_score < mate_score &&
      (tt_flag == hash_flag_exact ||
       (tt_flag == hash_flag_beta && tt_score > eval) ||
       (tt_flag == hash_flag_alpha && tt_score < eval)))
    eval = tt_score;

  // "Improving": is our static eval higher than it was two plies ago (our own
  // previous turn)? A rising trend means the position is going our way, so we
  // can trust forward pruning more and reduce more when it's NOT improving.
  // Meaningless in check (eval is noisy there) and at ply<2 (no history) ->
  // treated as not-improving. g_improving gates the whole heuristic so that,
  // when off, every consumer below collapses to the original search exactly.
  // FIX P0.6: se a ply-2 non c'era eval (nodo in scacco con LazyEval), fallback
  // a ply-4 (come SF); se nemmeno quella esiste -> not-improving (=
  // comportamento storico, senza pero' il confronto col vecchio 0 fittizio).
  bool improving = false;
  if (!in_check && td.ply >= 2) {
    int e2 = td.eval_stack[td.ply - 2];
    if (!g_improving_fix)
      improving =
          static_eval > e2; // legacy 3.7 (confronto anche col 0 fittizio)
    else if (e2 != EVAL_NONE)
      improving = static_eval > e2;
    else if (td.ply >= 4 && td.eval_stack[td.ply - 4] != EVAL_NONE)
      improving = static_eval > td.eval_stack[td.ply - 4];
  }

  // opponentWorsening (SF search.cpp :841): our static eval exceeds the negated
  // previous-ply eval -> the opponent's last move worsened their side -> we can
  // shave the RFP margin (prune a touch more). Gated like improving. Off = no
  // effect.
  bool opp_worsening = false;
  if (g_opp_worsening && !in_check && td.ply >= 1) {
    int e1 = td.eval_stack[td.ply - 1];
    if (e1 != EVAL_NONE)
      opp_worsening = static_eval > -e1;
  }

  // StatEvalDiffOrder (SF search.cpp ~728, "~9 Elo"): la differenza tra la
  // static eval di questo nodo e quella del padre e' un segnale a costo ~0
  // sulla qualita' della mossa PRECEDENTE (dell'avversario). Spingiamo la sua
  // history (main + pawn) col negato della somma delle due eval consecutive.
  // Gating come SF: nodo corrente NON in scacco (eval valida), padre NON in
  // scacco (eval_stack valida), ESISTE una mossa precedente, ed era QUIET.
  // g_seo_mult: 0=off, 14=SF, >14=aggr.
  if (g_seo_mult && !in_check && td.ply >= 1) {
    int prev_move = td.move_stack[td.ply];
    if (prev_move != 0 && td.captured_stack[td.ply] == -1 &&
        td.eval_stack[td.ply - 1] != EVAL_NONE) {
      int unscaled_eval_sum =
          (td.eval_stack[td.ply - 1] + static_eval) * 100 / nn_get_eval_scale();
      int bonus = -g_seo_mult * unscaled_eval_sum;
      if (bonus > 1455)
        bonus = 1455; // clamp PRIMA del raddoppio (come SF)
      if (bonus < -1723)
        bonus = -1723;
      bonus = bonus > 0 ? 2 * bonus : bonus / 2;
      int pp = get_move_piece(prev_move);
      int pt = get_move_target(prev_move);
      td_update_history(td.history_moves[td.hbucket_stack[td.ply]][pp][pt],
                        bonus);
      if (g_pawn_hist && pp != P && pp != p && !get_move_promoted(prev_move))
        td_update_history(
            td.pawn_history[corr_idx & ThreadData::PAWN_HIST_MASK][pp][pt],
            bonus / 4);
    }
  }

  // 5.1 HindsightExt (Pawnocchio): se QUESTO nodo non-PV e' stato RIDOTTO (LMR)
  // dal padre di
  // >= margine plies e l'eval e' girata male (static_eval + prev_eval < 0),
  // ri-estendi (+1): la riduzione era un errore "col senno di poi".
  // reduction_stack settato dal padre pre-recursione. Default OFF =
  // byte-identico (reduction_stack mai letto). Non in scacco/singular/PV/root.
  if (g_hindsight_ext && !pv_node && !excluded_move && !in_check &&
      td.ply >= 1 && td.eval_stack[td.ply - 1] != EVAL_NONE &&
      td.reduction_stack[td.ply] >= g_hindsight_margin &&
      static_eval + td.eval_stack[td.ply - 1] < 0)
    depth++;
  // 5.1 HindsightRed (Stormphrax #300, complemento): nodo ridotto >= margine ma
  // eval girata BENE (static_eval + prev_eval >= soglia) -> RIDUCI (-1): la
  // posizione regge, risparmia nodi. else-if -> mutuamente esclusivo con
  // l'extend (sum<0 vs sum>=soglia). depth>=2 per non azzerare.
  else if (g_hindsight_red && !pv_node && !excluded_move && !in_check &&
           depth >= 2 && td.ply >= 1 &&
           td.eval_stack[td.ply - 1] != EVAL_NONE &&
           td.reduction_stack[td.ply] >= g_hindsight_red_margin &&
           static_eval + td.eval_stack[td.ply - 1] >= g_hindsight_red_thresh)
    depth--;

  // Reverse futility pruning (skip when beta is a mate bound: don't cut a
  // potential mate search short based on a static evaluation). When improving,
  // shave one ply off the margin (easier cutoff): a rising eval is more likely
  // to hold above beta.
  int rfp_cap = g_rfp_depth_cap > 0 ? g_rfp_depth_cap : (g_rfp_depth8 ? 8 : 6);
  if (!pv_node && !in_check && depth <= rfp_cap && beta < mate_score &&
      !(g_rfp_no_ttpv && tt_hit && tt_pv) && // RFPNoTTPv (P5)
      !(g_excl_prune_gate >= 2 && excluded_move)) {
    // F-018.12 RFPHistThresh (0=off): se la hash move e' una quiet con history
    // scarsa, il fail-high statico non e' affidabile -> non tagliare (Berserk).
    bool rfp_ok = true;
    if (g_rfp_hist_thresh && tt_move && !get_move_capture(tt_move) &&
        !get_move_promoted(tt_move))
      rfp_ok =
          td.history_moves[td_hbucket(td, tt_move)][get_move_piece(tt_move)]
                          [get_move_target(tt_move)] > g_rfp_hist_thresh;
    // RFPTTMoveGate (SF step 9): con una TT move quiet il fail-high statico non
    // basta. Dopo il blocco RFPHistThresh, che altrimenti lo sovrascriverebbe.
    if (g_rfp_ttmove_gate && tt_move && !get_move_capture(tt_move) &&
        !get_move_promoted(tt_move))
      rfp_ok = false;
    int rfp_depth = depth - ((g_improving && improving) ? 1 : 0);
    int rfp_margin = g_rfp_margin * rfp_depth;
    if (opp_worsening)
      rfp_margin -= g_opp_worse_margin; // opp worse -> prune more
    if (g_rfp_badnode && !tt_move)
      rfp_margin -= g_rfp_badnode; // Q-20a: badNode -> prune more
    if (g_corrval_margin)
      rfp_margin += (corr_val < 0 ? -corr_val : corr_val) * g_corrval_rfp /
                    128; // heavy corr -> prune less
    if (g_corr_uncert)
      rfp_margin +=
          corr_uncert * g_cu_rfp /
          64; // P1a: tavole in disaccordo -> eval incerta -> pota meno
    if (g_rdr_rfp && td.root_depth > g_rdr_knee) // RootDepthRelax (Coda): ricerca profonda -> pota meno
      rfp_margin += rfp_depth * (td.root_depth - g_rdr_knee) * g_rdr_rfp / 100;
    if (rfp_margin < 0)
      rfp_margin = 0;
    if (rfp_ok && eval - rfp_margin >= beta)
      // F-018.7 FailHighSmooth: midpoint verso beta = bound TT meno gonfiato ->
      // meno re-search.
      return g_fh_smooth
                 ? td_fail_firm(eval - rfp_margin, beta, g_fh_t_rfp)
                 : eval - rfp_margin;
  }

  // Null move pruning (not when beta is a mate score)
  // P1.6 NMPVerif (default OFF): (a) mai due null consecutive (move_stack==0 =
  // il padre ha appena nullato); (b) durante una search di verifica niente
  // null.
  if (!pv_node && !in_check && td.ply && depth >= 3 && beta < mate_score &&
      eval >= beta && (!g_nmp_cutnode_only || is_cut_node) &&
      !(g_excl_prune_gate >= 1 && excluded_move) && // ExclPruneGate (P1)
      // F-018.8a NMPStaticMargin (default OFF): la STATIC eval (non quella
      // TT-improved) deve superare beta di un margine depth-dipendente (SF:
      // 21*depth - 421).
      (!g_nmp_static_margin ||
       static_eval >= beta - g_nmp_sm_mult * depth + g_nmp_sm_bias) &&
      // F-018.11 EasyCapGate (default OFF): con un nostro pezzo in presa
      // "facile" la null lo lascia li' un ply in piu' -> fail-high fasulli
      // (Berserk).
      (!g_easycap_gate || !td_has_easy_capture(td)) &&
      (!g_nmp_verif || (td.move_stack[td.ply] != 0 && !td.in_nmp_verif))) {
    U64 our_pieces = (td.side == white) ? (td.bitboards[N] | td.bitboards[B] |
                                           td.bitboards[R] | td.bitboards[Q])
                                        : (td.bitboards[n] | td.bitboards[b] |
                                           td.bitboards[r] | td.bitboards[q]);

    if (our_pieces) {
      int old_ep = td.enpassant;
      U64 old_hash = td.hash_key;

      if (td.enpassant != no_sq)
        td.hash_key ^= enpassant_keys[td.enpassant];
      td.enpassant = no_sq;
      td.side ^= 1;
      td.hash_key ^= side_key;

      td.ply++;
      td.reduction_stack[td.ply] =
          0; // HindsightExt: il figlio null-move non e' "ridotto-LMR"
      td.de_stack[td.ply] =
          td.de_stack[td.ply -
                      1]; // F-018.6b: la catena double-ext attraversa la null
      td.repetition_table[++td.repetition_index] = td.hash_key;

      // Null move: the child has no "previous move" for counter-move.
      td.move_stack[td.ply] = 0;
      td.captured_stack[td.ply] = -1;

      // P2.2: il sottoalbero della null parte con 0 mosse reali dall'ultima
      // null (le ripetizioni non si contano attraverso una null, stile SF).
      const int saved_pfn = td.plies_from_null;
      td.plies_from_null = 0;

      int R = g_nmp_base + depth / g_nmp_div;
      // (1) NMPEvalScale: più sopra beta = riduzione maggiore (cap +3). OFF =
      // invariato.
      if (g_nmp_eval_scale) {
        // 🔴 `static_eval`, NON `eval`: SF usa ss->staticEval, e la variante sulla
        // eval TT-corretta e' quella che avevano revertato. E nessun cap: il loro
        // max(...,0) taglia solo sotto. Con /256 la riduzione extra cresce piano.
        int e = (static_eval - beta) / g_nmp_eval_div;
        if (e > 0)
          R += e;
      }
      // F-018.8b NMPTTNoisy (default OFF): TT move cattura = posizione tattica
      // gia' "risolta" da una noisy -> la null puo' permettersi un ply in piu'
      // (Obsidian).
      if (g_nmp_ttnoisy && tt_move && get_move_capture(tt_move))
        R++;
      if (R > depth - 1)
        R = depth - 1;

      nn_pos_do_null(td.nnpos, td.fifty);
      score = -td_negamax(td, -beta, -beta + 1, depth - 1 - R, !is_cut_node);
      nn_pos_undo(td.nnpos);

      td.plies_from_null = saved_pfn; // P2.2 restore
      td.ply--;
      td.repetition_index--;
      td.side ^= 1;
      td.hash_key = old_hash;
      td.enpassant = old_ep;

      if (stop_threads.load(std::memory_order_relaxed))
        return 0;
      // fail-soft: ritorna lo score reale, ma non un matto "finto" da null
      // move.
      if (score >= beta) {
        // P1.6 NMPVerif: a depth alta il fail-high della null va CONFERMATO
        // da una search REALE ridotta (anti-zugzwang). Durante la verifica
        // la null e' disattivata (td.in_nmp_verif). Verifica fallita = non
        // potare: il nodo prosegue normalmente.
        if (g_nmp_verif && depth >= g_nmp_verif_depth && !td.in_nmp_verif &&
            score < mate_score) {
          td.in_nmp_verif = true;
          int v = td_negamax(td, beta - 1, beta, depth - 1 - R, is_cut_node);
          td.in_nmp_verif = false;
          if (stop_threads.load(std::memory_order_relaxed))
            return 0;
          if (v >= beta)
            return (v >= mate_score) ? beta : v;
        } else {
          return (score >= mate_score) ? beta : score;
        }
      }
    }
  }

  // Razoring
  int razor_cap =
      g_razor_depth_cap > 0 ? g_razor_depth_cap : (g_razor_depth4 ? 4 : 3);
  bool razor_ok = true;
  if (g_razor_ttquiet) // razora solo con TT move rumorosa (nessuna TT move =>
                       // niente razoring)
    razor_ok = tt_move != 0 &&
               (get_move_capture(tt_move) || get_move_promoted(tt_move));
  if (g_razor_ttlower && tt_hit && tt_flag == hash_flag_beta)
    razor_ok = false; // bound LOWER = ha gia' fallito alto
  if (!pv_node && !in_check && razor_ok && depth <= razor_cap &&
      !(g_excl_prune_gate >= 2 && excluded_move)) {
    int razor_margin =
        g_razor_base + g_razor_mult * depth +
        g_razor_quad_coef * depth * depth; // coef 0 = legacy linear
    if (eval + razor_margin < alpha) {
      score = td_quiescence(td, alpha, beta);
      if (score < alpha)
        return score;
    }
  }

  // ProbCut: at sufficient depth, if some capture leads (via a cheap qsearch +
  // reduced-depth confirmation) to a score well above beta, the full-depth
  // search would almost certainly fail high too -> prune now. Captures only,
  // SEE-gated so we only try moves whose material swing can bridge the margin.
  // Skipped in PV / in check / near mate / during a singular search, and when a
  // deep-enough TT entry already proves the value stays below probcut_beta.
  if (g_probcut && !pv_node && !in_check && !excluded_move &&
      depth >= g_probcut_min_depth && beta < mate_score && beta > -mate_score) {
    int probcut_beta = beta + g_probcut_margin -
                       ((g_improving && improving) ? g_probcut_improve : 0);
    if (g_rdr_probcut && td.root_depth > g_rdr_knee) // RootDepthRelax (Coda)
      probcut_beta += (td.root_depth - g_rdr_knee) * g_rdr_probcut;
    bool tt_blocks = tt_hit && tt_depth >= depth - 3 &&
                     tt_score < probcut_beta && tt_score > -mate_score;
    if (!tt_blocks) {
      moves pc_list[1];
      td_generate_moves(td, pc_list, true); // captures only
      int pc_scores[256];
      td_score_all_moves(td, pc_list, tt_move, pc_scores);

      for (int count = 0; count < pc_list->count; count++) {
        // Pick-next: bring the best-scored remaining move to the front.
        int best_idx = count;
        for (int i = count + 1; i < pc_list->count; i++)
          if (pc_scores[i] > pc_scores[best_idx])
            best_idx = i;
        if (best_idx != count) {
          int tm = pc_list->moves[count];
          pc_list->moves[count] = pc_list->moves[best_idx];
          pc_list->moves[best_idx] = tm;
          int ts = pc_scores[count];
          pc_scores[count] = pc_scores[best_idx];
          pc_scores[best_idx] = ts;
        }
        int move = pc_list->moves[count];

        if (!get_move_capture(move))
          continue;
        // The capture's material swing must be able to bridge the eval
        // up to probcut_beta, else it can't plausibly cause the cutoff.
        // BUG FIX 2026-07-16: td_see NON conta il guadagno di promozione
        // (~+800cp per Q)
        // -> sottovaluta una capture-promotion vincente e la filtra fuori dal
        // probcut = cutoff speculativo mancato. Le capture-promotion passano
        // sempre il gate SEE.
        if (!get_move_promoted(move) &&
            !td_see_at_least(td, move, probcut_beta - eval))
          continue;

        UndoInfo undo;
        // A1 FIX 2026-07-25: il figlio probcut inizializzava SOLO reduction_stack.
        // seen/de/hbucket/captured restavano quelli lasciati a questo ply dal
        // fratello precedente, e il figlio li LEGGE: move_stack != 0 (riga sotto)
        // apre i blocchi SEO (:6441) e TTCutMalus (:6173), che con un
        // captured_stack stale a -1 scrivono bonus/malus nella history QUIET per
        // una CATTURA, premiano la vittima sbagliata in capture_history via
        // hbucket_stack, e con un de_stack sporco abilitano doppia/tripla
        // estensione ereditata da un sottoalbero fratello (:7101).
        // Che fosse una dimenticanza lo prova reduction_stack, che c'era.
        // ponytail: terzo sito che spinge un ply figlio (main loop :7213,
        // null-move :6550, questo) con la stessa lista a mano. Se ne nasce un
        // quarto, estrarre un td_push_child() invece di ricopiarla.
        // Gate 2026-07-25: con questi 4 assegnamenti disattivati il bench torna
        // esattamente a 275063 (= la release pre-fix), quindi il delta e' TUTTO
        // di A1: 275063 -> 283729, +3.15% di albero.
        int pc_hbucket = td_hbucket(td, move); // pre-make: legge la board del padre
        td.seen_stack[td.ply] =
            count; // mosse viste a QUESTO nodo (il figlio le legge su TT-cut)
        td.ply++;
        td.reduction_stack[td.ply] =
            0; // HindsightExt: probcut child non e' "ridotto-LMR"
        td.de_stack[td.ply] =
            td.de_stack[td.ply - 1]; // il probcut non estende: la catena passa
        td.repetition_table[++td.repetition_index] = td.hash_key;
        if (!td_make_move(td, move, undo)) {
          td.ply--;
          td.repetition_index--;
          continue;
        }
        td.move_stack[td.ply] = move;
        td.hbucket_stack[td.ply] = pc_hbucket;
        td.captured_stack[td.ply] = undo.captured_piece;

        // Cheap qsearch screen first; only if it clears probcut_beta do we
        // pay for the reduced-depth (depth-4) confirmation search.
        // Q-10 ProbCutImproving (default OFF): -1 ply quando improving
        // (depth 0 -> td_negamax cade in qsearch, come SF/Reckless).
        int pc_depth = depth - g_probcut_depth_off -
                       ((g_probcut_improving && improving) ? 1 : 0);
        int pc_score = -td_quiescence(td, -probcut_beta, -probcut_beta + 1);
        if (pc_score >= probcut_beta)
          pc_score = -td_negamax(td, -probcut_beta, -probcut_beta + 1, pc_depth,
                                 !is_cut_node);

        td_unmake_move(td, move, undo);
        td.ply--;
        td.repetition_index--;

        if (stop_threads.load(std::memory_order_relaxed))
          return 0;

        if (pc_score >= probcut_beta) {
          // N2 ProbCutTT (SF): salva il fail-high in TT a pc_depth+1 (= depth-3
          // col Q-10 off) cosi' le rivisite tagliano dal probe senza rifare
          // qsearch+verifica.
          if (g_probcut_tt && !excluded_move)
            store_tt(td.hash_key, move, pc_score, pc_depth + 1, hash_flag_beta,
                     td.ply, store_pv,
                     tt_store_eval(node_raw_eval, tt_eval, tt_flag, td.fifty));
          // ProbCutAdj (P4): valore riportato verso beta del margine usato.
          if (g_probcut_adj && pc_score < mate_score - 2 * max_ply)
            return pc_score - (probcut_beta - beta);
          return pc_score; // fail-soft prune
        }
      }
    }
  }

  // ---- Move generation & ordering ------------------------------------------
  // Staged MovePicker (g_move_picker) produces moves lazily by stage, so an
  // early cutoff never pays to generate/score the quiet moves. Legacy path:
  // generate everything and score up-front (pick-next selection).
  const bool use_picker = g_move_picker;

  moves move_list[1]; // legacy: all moves | staged: quiet-stage buffer
  int move_scores[256];
  moves cap_buf[1]; // staged: capture/promotion-stage buffer
  int cap_score_buf[256];

  if (!use_picker) {
    td_generate_moves(td, move_list);
    // Assegna punteggi senza ordinare l'array (la selezione e' pick-next).
    td_score_all_moves(td, move_list, tt_move, move_scores);
  }

  MovePicker mp;
  mp_init(mp, td, use_picker, tt_move, cap_buf, cap_score_buf, move_list,
          move_scores, in_check ? 1 : 0);

  int best_move = 0;
  int best_score = -infinity;
  int hash_flag = hash_flag_alpha;
  int legal_moves = 0;

  // Node-based time management: at the root, track how many nodes the best move
  // costs. Reset per iteration; updated when a move becomes the new best.
  const bool is_root_node = (td.ply == 0);
  if (is_root_node)
    td.root_bestmove_nodes = 0;

  // Q-11 NodeCache (Caissa Search.cpp:1683-1688): entry per QUESTO nodo se e'
  // vicino alla radice. Riempita dopo ogni figlio (nc_add sotto). nullptr =
  // niente tracking.
  ThreadData::NCEntry *nc = nullptr;
  if (g_node_cache && td.ply < ThreadData::NC_MAX_PLY && !excluded_move)
    nc = nc_get(td, td.hash_key);
  // R-PB: la TT-move di QUESTO nodo, letta dal figlio come "la mossa che mi ha
  // generato era la TT-move di mio padre?". Scritta una volta sola qui, non nel
  // move loop: non dipende dalla mossa in corso.
  td.ttmove_stack[td.ply] = tt_move;


  int moves_searched = 0;
  int quiets_searched = 0;
  // PruneNPMGate (P6): true = le potature del ciclo mosse sono ammesse.
  const bool mp_prune_ok =
      !g_prune_npm_gate ||
      ((td.side == white) ? (td.bitboards[N] | td.bitboards[B] | td.bitboards[R] |
                             td.bitboards[Q])
                          : (td.bitboards[n] | td.bitboards[b] | td.bitboards[r] |
                             td.bitboards[q])) != 0;
  int bad_caps_pruned = 0; // BadCapSkipAfter: bad capture SEE-potate a questo nodo
  LmpChkCtx
      lmp_chk; // LMPCheckGuard: check-squares del nodo, riempite alla prima LMP

  // Quiet moves searched at this node (for history malus on a beta cutoff).
  int searched_quiets[64];
  int n_searched_quiets = 0;

  int searched_captures[64];
  int n_searched_captures = 0;

  // F-018.13 KillerReset (default OFF): azzera i killer del ply FIGLIO prima
  // del loop mosse — i killer ereditati da sottoalberi FRATELLI sono stale
  // (Obsidian/Berserk).
  if (g_killer_reset && td.ply + 1 < max_ply + 8) {
    td.killer_moves[0][td.ply + 1] = 0;
    td.killer_moves[1][td.ply + 1] = 0;
  }

  int move;
  while ((move = mp_next(td, mp)) != 0) {
    // Ora move   garantita essere la migliore
    // `move` is supplied by mp_next() in the while condition above.
    if (move == excluded_move)
      continue; // singular search: skip the TT move
    // P4 TroubleMaking: verifica del candidato -> alla root SOLO quella mossa
    if (is_root_node && td.root_only_move && move != td.root_only_move)
      continue;
    // searchmoves (analisi): se una whitelist e' attiva, alla root cerca SOLO
    // quelle mosse. Vale per la linea principale e per tutte le linee MultiPV.
    if (is_root_node && g_searchmoves_count) {
      bool in_list = false;
      for (int k = 0; k < g_searchmoves_count; k++)
        if (g_searchmoves[k] == move) {
          in_list = true;
          break;
        }
      if (!in_list)
        continue;
    }
    // MultiPV: alla root salta le best move gia' assegnate alle linee
    // precedenti (NON via excluded_move: quello e' del singular e sopprime lo
    // store TT).
    if (is_root_node && td.mpv_count) {
      bool mpv_skip = false;
      for (int k = 0; k < td.mpv_count; k++)
        if (td.mpv_excluded[k] == move) {
          mpv_skip = true;
          break;
        }
      if (mpv_skip)
        continue;
    }
    bool is_capture = get_move_capture(move);
    bool is_promotion = get_move_promoted(move);
    bool is_quiet = !is_capture && !is_promotion;

    // LmrDepthPrune: profondita' su cui gating-are futility+conthist. Di
    // default = depth piena (byte-identico). Con g_lmrdepth_prune: depth
    // ridotta dalla LMR per QUESTA mossa (mosse tardive -> riduzione grande ->
    // prune_depth piccola -> pota anche a node-depth alto, come SF). Cap nostri
    // (6/4) restano = meno aggressivo.
    int prune_depth = depth;
    if (g_lmrdepth_prune && is_quiet && depth >= 3 && moves_searched >= 1) {
      int d_idx = depth < 64 ? depth : 63;
      int m_idx = moves_searched < 64 ? moves_searched : 63;
      prune_depth = depth - 1 - lmr_table[d_idx][m_idx];
      // PASSO 1 (SF): protezione-history. prune_depth += history/div -> le
      // mosse con buona storia (probabili cutoff) salgono e NON vengono potate;
      // le scarse scendono. SF: history = 2*mainHist + contHist + pawnHist. E'
      // cio' che mancava al port monco.
      int prev_m = td.move_stack[td.ply];
      int hist =
          2 * td.history_moves[td_hbucket(td, move)][get_move_piece(move)]
                              [get_move_target(move)];
      if (prev_m)
        hist += td.continuation_history[get_move_piece(prev_m)][get_move_target(
            prev_m)][get_move_piece(move)][get_move_target(move)];
      if (g_pawn_hist)
        hist += td.pawn_history[td_corr_index(td) & ThreadData::PAWN_HIST_MASK]
                               [get_move_piece(move)][get_move_target(move)];
      prune_depth += hist / g_lmrdepth_histdiv;
      if (prune_depth < 0)
        prune_depth = 0;
    }

    // LMP  (Q-15: mai sui nodi che seguono la PV — l'ordinamento li' e' gia'
    // fidato)
    if (mp_prune_ok && !pv_node && !in_check && is_quiet && best_score > -mate_score &&
        !follow_pv) {
      int lmp_threshold = -1; // -1 = nessun move-count pruning a questo nodo
      if (g_lmp_improving) {
        // P1.7 (default OFF, co-tune): formula SF (base + d^2*quad/100) /
        // (2 - improving), SENZA cap di profondita'. LMPScale resta la
        // scala comune.
        lmp_threshold = (g_lmp_base + depth * depth * g_lmp_quad / 100) /
                        (2 - ((g_improving && improving) ? 1 : 0));
        lmp_threshold = lmp_threshold * g_lmp_scale / 100;
      } else if (depth <= 8) {
        int lmp_idx = (depth < 8) ? depth : 8;
        lmp_threshold = lmp_table[lmp_idx] * g_lmp_scale /
                        100; // LMPScale (100 = invariato)
      }
      if (g_rdr_lmp && lmp_threshold >= 0 && td.root_depth > g_rdr_knee) // RootDepthRelax (Coda)
        lmp_threshold += (td.root_depth - g_rdr_knee) * g_rdr_lmp / 10;
      if (lmp_threshold >= 0 && quiets_searched >= lmp_threshold) {
        if (g_lmp_check_guard) {
          // Guard: le quiet che danno SCACCO non si potano mai (forzanti:
          // tattica e matti si trovano tardissimo se le tagli). Qui NON si puo'
          // spegnere l'intero stage quiet — una quiet piu' tardiva potrebbe
          // dare scacco — quindi si paga il test mossa per mossa invece dello
          // skip di stage.
          if (!td_lmp_gives_check(td, move, lmp_chk))
            continue;
        }
        // Phase-2: with the staged picker we can skip the entire quiet
        // stage instead of testing every move individually. The flag is
        // set once; mp_next() will never enter MPS_GEN_QUIET again.
        else if (use_picker) {
          mp.skip_quiets = true;
          continue;
        } else
          continue;
      }
    }

    // Futility pruning. When improving, widen the margin so we prune fewer
    // quiets (a rising eval deserves the benefit of the doubt); when not
    // improving, the base margin prunes more.
    if (mp_prune_ok && !pv_node && !in_check && prune_depth <= g_fut_depth && is_quiet &&
        best_score > -mate_score && !follow_pv &&
        (!g_fut_spare_quiet ||
         quiets_searched >= 1)) { // Q-15 + Q-20c (risparmia la prima quiet)
      int futility_margin = g_fut_base + g_fut_mult * prune_depth +
                            ((g_improving && improving) ? g_fut_improving : 0);
      if (g_corrval_ext)
        futility_margin += (corr_val < 0 ? -corr_val : corr_val) *
                           g_corrval_fut / 128; // heavy corr -> prune less
      if (g_corr_uncert)
        futility_margin +=
            corr_uncert * g_cu_fut / 64; // P1a: disaccordo corr -> pota meno

      if (eval + futility_margin <= alpha) {
        // FutFailSoft (P7): il fail-low restituisce almeno il valore di futility.
        if (g_fut_fail_soft) {
          const int fv = eval + futility_margin;
          if (fv > best_score && best_score < mate_score - 2 * max_ply &&
              fv < mate_score - 2 * max_ply)
            best_score = fv;
        }
        // Phase-2: once futility fires, ALL remaining quiets at this node
        // fail the same static-eval test -> skip the entire stage.
        // ...tranne quando c'e' un pedone in zona promozione: li' lo stage va
        // percorso fino in fondo, altrimenti l'esenzione qui sopra e' inutile
        // perche' la spinta non verrebbe mai generata.
        if (use_picker) {
          mp.skip_quiets = true;
        }
        quiets_searched++;
        continue;
      }
    }

    // Capture futility pruning (SF master Step 14). A capture searched at low
    // (reduced) depth that can't lift the static eval to alpha even after
    // winning the victim (plus a capture-history bias) is pruned. Good captures
    // carry a large see_piece_values[vic] term -> never pruned; only futile
    // (material-neutral/losing) captures at a node already below alpha get cut.
    // Like our quiet futility / qsearch capture-futility, this fires before
    // make (no pre-move gives-check gate, matching the quiet path).
    // NB: il termine vittima va convertito cp -> unita'-eval (vedi
    // g_capfut_vic_scale). Fino al 2026-07-16 entrava a 1x: sotto-pesato ~2.4x
    // -> si potavano proprio le catture buone che le due righe qui sopra
    // dichiarano intoccabili.
    if (mp_prune_ok && g_cap_futility && !pv_node && !in_check && is_capture &&
        !is_promotion && best_score > -mate_score) {
      int d_idx = depth < 64 ? depth : 63;
      int m_idx = moves_searched < 64 ? moves_searched : 63;
      int cap_lmr_depth =
          depth - 1 - lmr_table[d_idx][m_idx]; // reduced-depth estimate
      if (cap_lmr_depth < 0)
        cap_lmr_depth = 0;
      if (cap_lmr_depth < g_capfut_depth) {
        int tgt = get_move_target(move);
        int vic = P; // default pawn (also correct for en passant)
        int s = (td.side == white) ? p : P;
        int e = (td.side == white) ? k : K;
        for (int pc = s; pc <= e; pc++)
          if (get_bit(td.bitboards[pc], tgt)) {
            vic = pc;
            break;
          }
        int ch = td.capture_history[get_move_piece(move)][tgt][vic]
                                   [td_cbucket(td, tgt)];
        int vic_ev = (int)((long long)see_piece_values[vic] *
                           g_capfut_vic_scale * nn_get_eval_scale() / 10000);
        int fut = eval + g_capfut_base + g_capfut_mult * cap_lmr_depth +
                  vic_ev + g_capfut_chist * ch / 1024;
        if (fut <= alpha)
          continue; // captures don't bump quiets_searched
      }
    }

    // SEE pruning: scarta a bassa profondita' le catture in perdita oltre un
    // margine, prima di cercarle (riduttore di nodi). Promozioni escluse.
    // S-05 SEELmrDepth (default OFF, corretto 2026-07-06): per le QUIET usa
    // prune_depth (LMR-ridotta) SENZA il cap g_see_depth (come SF Step 14:
    // nessun cap — il quadrato in prune_depth svanisce da solo alle mosse non
    // tardive/non ridotte, es. a prune_depth=15 il margine e' -185*225, mai
    // raggiungibile). Le catture restano invariate (prune_depth==depth per loro
    // comunque; SF le compensa con un termine capthist che non abbiamo ancora —
    // non toccarle senza quello).
    bool see_lmr_path = is_quiet && (g_see_lmr_depth || g_see_lmr_linear);
    int see_gate_depth = see_lmr_path ? prune_depth : depth;
    bool see_gate_ok = see_lmr_path ? (see_gate_depth <= g_see_lmr_prune_cap)
                                    : (see_gate_depth <= g_see_depth);
    if (mp_prune_ok && !pv_node && !in_check && !is_promotion && see_gate_ok &&
        best_score > -mate_score) {
      int quiet_margin_coef =
          see_lmr_path ? g_see_lmr_quiet_margin : g_see_quiet_margin;
      int see_margin =
          is_capture ? (-g_see_cap_margin * depth)
          : (see_lmr_path && g_see_lmr_linear)
              ? (-g_see_lmr_linear_coef * see_gate_depth) // S-05 forma lineare
              : (-quiet_margin_coef * see_gate_depth * see_gate_depth);
      if (g_corrval_ext)
        see_margin -= (corr_val < 0 ? -corr_val : corr_val) * g_corrval_see /
                      128; // heavy corr -> prune less (soglia piu' negativa)
      if (!td_see_at_least(td, move, see_margin)) {
        // S-11 (SF, default OFF): se siamo sotto (alpha<0) e la cattura muove
        // il nostro ULTIMO pezzo non-pedone, il sacrificio puo' essere la
        // risorsa di STALLO -> non potarla (falla cercare). Fires raro.
        bool stalemate_resource = false;
        if (g_see_stalemate_guard && is_capture && alpha < 0) {
          int mpc = get_move_piece(move);
          bool nonpawn_mover = (td.side == white) ? (mpc >= N && mpc <= Q)
                                                  : (mpc >= n && mpc <= q);
          if (nonpawn_mover) {
            U64 others = (td.side == white)
                             ? (td.bitboards[N] | td.bitboards[B] |
                                td.bitboards[R] | td.bitboards[Q])
                             : (td.bitboards[n] | td.bitboards[b] |
                                td.bitboards[r] | td.bitboards[q]);
            stalemate_resource = count_bits(others) == 1;
          }
        }
        if (!stalemate_resource) {
          if (is_quiet)
            quiets_searched++;
          // Phase-2: le bad capture escono in ordine di SCORE decrescente, NON
          // di SEE decrescente (td_score_move: SCORE_BAD_CAPTURE + mvv_lva +
          // caphist). "Questa ha fallito -> tutte le prossime falliscono" e'
          // quindi FALSO: dietro una cattura di vittima grossa e SEE pessimo
          // puo' esserci una cattura di vittima piccola con SEE migliore, che
          // questo stesso test avrebbe fatto passare. BadCapSkipAfter = quante
          // prove servono prima di spegnere lo stage (1 = storico, 0 = mai).
          // L'ordine dei termini e' load-bearing: senza `g_badcap_skip_after &&`
          // il valore 0 darebbe `++c >= 0` sempre vero, cioe' il comportamento
          // PIU' aggressivo travestito da "spento".
          if (use_picker && is_capture && mp.stage == MPS_BAD_TACTICAL &&
              g_badcap_skip_after &&
              ++bad_caps_pruned >= g_badcap_skip_after)
            mp.skip_bad_caps = true;
          continue;
        }
      }
    }

    // History pruning: a late quiet move whose combined (butterfly +
    // continuation) history is strongly negative has repeatedly failed in this
    // context -> skip it at low depth. Threshold scales with depth so we prune
    // more aggressively the shallower we are. NB: td.ply not yet incremented
    // here, so the previous move is td.move_stack[td.ply].
    if (mp_prune_ok && g_cont_hist_prune && !pv_node && !in_check && is_quiet &&
        prune_depth <= g_conthist_prune_depth && moves_searched > 0 &&
        best_score > -mate_score) {
      int prev = td.move_stack[td.ply];
      int hh = td.history_moves[td_hbucket(td, move)][get_move_piece(move)]
                               [get_move_target(move)];
      if (prev)
        hh += td.continuation_history[get_move_piece(prev)][get_move_target(
            prev)][get_move_piece(move)][get_move_target(move)];
      if (hh < -g_histprune_margin * depth) {
        quiets_searched++;
        continue;
      }
    }

    // Singular extension: before searching the TT move, check whether it is
    // much better than every alternative. Re-search this position EXCLUDING
    // the TT move at reduced depth with a window just below tt_score; if all
    // other moves fail low, the TT move is "singular" and gets +1 ply.
    // g_singular_dmargin: how far below singular_beta the alternatives must
    // fall for a DOUBLE extension (bigger = rarer/safer). SPSA-tunable.
    int extension = 0;
    // F-018.6a SingularMinDepth (default 8 = storico; Obsidian 5, Berserk 6: a
    // 6 il blocco scatta ~2x piu' spesso). F-018.6c SingularPlyGuard:
    // anti-esplosione oltre ply >= 2*rootDepth (entrambi i rivali ce l'hanno).
    if (excluded_move == 0 && move == tt_move && depth >= g_singular_mindepth &&
        td.ply && (!g_singular_plyguard || td.ply < 2 * td.root_depth) &&
        tt_hit && tt_depth >= depth - g_singular_ttmargin &&
        tt_flag != hash_flag_alpha && tt_score > -mate_score &&
        tt_score < mate_score) {
      // F-002: era hardcoded 2*depth. SingularExactMargin: bound EXACT ->
      // margine dimezzato (score fidato -> piu' facile dichiarare singolare ->
      // piu' estensioni dove serve). Margine PIENO: resta l'ancora per
      // doppia/tripla estensione (vedi sotto).
      const int singular_margin_full = g_singular_mpd * depth;
      // ⭐ SingularExactMaxDepth (0 = nessun cap = byte-identico): il
      // dimezzamento su bound EXACT cambia NATURA con la profondita'. Albero vs
      // baseline (bench VM, 19/07):
      //   depth 13 -7.4% | depth 17 -11.1% | depth 20 **+42.7%**
      // Sotto ~18 stringe (ed e' li' che a 10+0.1 valeva +8.60); sopra esplode,
      // ed e' la spiegazione del -8.60 misurato a 20+0.2. Cap = tenere il
      // beneficio e buttare il danno, stesso schema del TC-gate di TMv2.
      const bool exact_halve = g_singular_exact_margin &&
                               tt_flag == hash_flag_exact &&
                               (g_singular_exact_maxdepth <= 0 ||
                                depth <= g_singular_exact_maxdepth);
      int singular_beta = tt_score - (exact_halve ? singular_margin_full / 2
                                                  : singular_margin_full);
      // ⚠️ SingularExactDecouple (2026-07-19, default OFF = byte-identico).
      // Il dimezzamento su bound EXACT alza singular_beta, e le soglie di
      // doppia/tripla estensione sono misurate DA singular_beta -> si alzano
      // anche loro. Effetto voluto: piu' facile essere SINGOLARE. Effetto NON
      // voluto: piu' facile essere DOPPIO/TRIPLO. E poiche' il margine e'
      // `mpd*depth`, lo scarto assoluto cresce con la profondita' (a d8 sono 4
      // unita', a d24 sono 12): a TC lungo la cascata di doppie/triple esplode.
      // Sospetto per l'inversione di segno misurata: +8.60 @10+0.1 -> -8.60
      // @20+0.2. Con questo ON, doppia/tripla restano ancorate al margine
      // PIENO.
      const int de_beta = g_singular_exact_decouple
                              ? (tt_score - singular_margin_full)
                              : singular_beta;
      int singular_depth = (depth - 1) / g_singular_depth_div;
      // Guardia 12/08/2026 — BYTE-IDENTICA coi default (mindepth 6, div 2 =>
      // singular_depth >= 2), quindi non tocca nulla oggi. Serve a rendere
      // SingularDepthDiv TARABILE senza trappola: basta che uno SPSA lo alzi a
      // >= depth-1 perche' singular_depth diventi 0, e `td_negamax` a depth 0
      // cade in quiescenza — la verifica di singolarita' smetterebbe di essere
      // una ricerca e diventerebbe un qsearch, cambiando significato invece di
      // grado. Un parametro che puo' rendere insensato il proprio test non e'
      // tarabile: o la guardia c'e', o il parametro sta fuori dallo spazio SPSA.
      if (singular_depth < 1)
        singular_depth = 1;
      int s = td_negamax(td, singular_beta - 1, singular_beta, singular_depth,
                         is_cut_node, tt_move);
      // Audit SF#5612 (2026-07): ricerca di esclusione abortita -> s=0 garbage
      // deciderebbe estensioni/multicut (e con MulticutTTMalus on, un malus
      // history spurio). Stessa guardia post-figlio del move loop.
      if (stop_threads.load(std::memory_order_relaxed))
        return 0;
      if (s < singular_beta) {
        extension = 1;
        // Double extension: the TT move beats every alternative by a wide
        // margin (hyper-forced) -> extend 2 plies. Non-PV only, to bound the
        // search blow-up that uncapped double extensions can cause.
        // F-018.6b SingularDECap (0=off): limita la CATENA di double-extension
        // sul path (Berserk ss->de <= 6) — senza cap le catene lunghe esplodono
        // l'albero.
        if (g_singular_ext && !pv_node && s < de_beta - g_singular_dmargin &&
            (!g_singular_de_cap || td.de_stack[td.ply] <= g_singular_de_cap)) {
          extension = 2;
          // Triple extension (SF :1244): beats alternatives by an even wider
          // margin -> +3. tmargin > dmargin so this nests inside the double.
          if (g_triple_ext && s < de_beta - g_singular_tmargin)
            extension = 3;
        }
      }
      // Multi-cut (BAKED, default on): the exclusion search (TT move removed)
      // failed high and singular_beta itself is >= beta -> more than one move
      // beats beta -> this expected cut-node is not singular -> prune the whole
      // subtree by returning singular_beta. Conservative gate on the BOUND (not
      // the value): fires rarely, only on a very high TT score, which keeps it
      // safe and +Elo where the aggressive SF gate (s>=beta) over-pruned (-13
      // Elo). Non-PV only (unsound in PV). Safe to return: no move has been
      // made yet this iteration.
      else if (g_multicut && !pv_node && singular_beta >= beta) {
        // MulticutTTMalus: la ricerca di esclusione ha fallito alto SENZA la
        // ttMove -> la posizione vince comunque -> la ttMove non era la ragione
        // del vantaggio. Malus alla sua history (solo se quiet: le catture
        // hanno una tabella a parte).
        if (g_multicut_ttmalus && !get_move_capture(tt_move) &&
            !get_move_promoted(tt_move)) {
          int malus = td_stat_bonus(depth) * g_multicut_ttmalus_scale / 100;
          td_update_history(
              td.history_moves[td_hbucket(td, tt_move)][get_move_piece(tt_move)]
                              [get_move_target(tt_move)],
              -malus);
        }
        // ⭐ MulticutCorr (port SF 218c74ec, idea PlentyChess): la ricerca di
        // esclusione ha fallito alto SENZA la ttMove, quindi il valore vero del nodo
        // e' almeno `s`. Se `s` supera la static eval, quella e' una sottostima
        // MISURATA — il campione esatto che la correction history serve a imparare, e
        // fino a oggi lo buttavamo via tornando da qui senza scrivere niente.
        // ⚠️ Si impara da `s`, non da `singular_beta`: SF usa il risultato della
        // ricerca (il loro `value`), mentre singular_beta e' solo il bound
        // conservativo che restituiamo.
        // Guardie, tutte mutuate da td_corr_update e nessuna cosmetica:
        //  - excluded_move: registrare informazione sulla POSIZIONE mentre una mossa
        //    e' esclusa significa registrarla falsa (vedi SingularNoPollute a :1286,
        //    dove la stessa asimmetria e' documentata e voluta).
        //  - banda TB: un verdetto Syzygy non e' una valutazione ma una certezza;
        //    entrerebbe come errore di eval da ~29.900, saturerebbe il bucket e
        //    manderebbe `decay = cv*abs_bonus/lim` in overflow di int (vedi :6978).
        //  - il clamp a lim/4 e' di SF, ed e' anche cio' che rende impossibile
        //    l'overflow qui: senza, `s - static_eval` puo' valere decine di migliaia.
        if (g_multicut_corr && g_corr_hist && !in_check && !excluded_move &&
            s > static_eval) {
          const int corr_max = g_corr_tb_guard ? mate_score - 2 * max_ply
                                               : mate_score - max_ply;
          if (s < corr_max && s > -corr_max && static_eval < corr_max &&
              static_eval > -corr_max) {
            const int w = singular_depth * g_multicut_corr_scale / 100;
            if (w > 0) {
              const int lim = g_corr_cap * CORR_GRAIN;
              long long bonus =
                  (long long)(s - static_eval) * CORR_GRAIN * w / g_corr_lr_div;
              if (bonus > lim / 4)
                bonus = lim / 4; // `s > static_eval` => bonus > 0: un solo lato
              td_corr_bonus(td, corr_idx, (int)bonus, g_corr_lr_div);
            }
          }
        }
        return singular_beta;
      }
      // Negative extensions as tunable amounts (0 = off). ttMove assumed to
      // fail high over beta -> shrink by g_negext_tt (default 2, SF=3); else on
      // a cut node -> shrink by g_negext_cut (default 3, SF=2).
      // ⚠️ I DEFAULT nel commento erano stantii: diceva "g_negext_tt default
      // 1=legacy" e "g_negext_cut default 0=off" mentre le globali a :1240 e
      // :1260 valgono 2 e 3. Il secondo per giunta e' il massimo del suo range,
      // e viene da uno SPSA che tarava rumore — vedi il blocco qui sotto.
      // 🔴 NegExtCut E' CODICE MORTO CON L'ORDINE STORICO — trovato il 7/08/2026.
      // I due rami sopra PARTIZIONANO tutto lo spazio: su un nodo non-PV la
      // finestra e' nulla (alpha = beta-1), quindi ogni tt_score intero e' o
      // >= beta o <= alpha, non esiste il mezzo. E `is_cut_node` vive solo sui
      // non-PV. ⇒ il terzo ramo non ha nessun caso residuo in cui cadere.
      // Verificato sul bench: NegExtCut a 0,1,2,3,4 da' TUTTI 212420 nodi.
      // ⚠️ Il default e' 3 = MASSIMO del range: viene da un giro di SPSA che ha
      // tarato rumore su un parametro inerte. Va tolto dallo spazio SPSA finche'
      // NegExtOrder resta 0.
      //
      // Stockfish ha le stesse tre idee ma in ordine diverso, ed e' l'ordine a
      // renderle tutte vive: mette `cutNode` PRIMA del confronto con alpha (e il
      // suo terzo test e' `ttValue <= value`, contro il risultato della ricerca
      // singular, che non e' il complemento del primo).
      //
      // NegExtOrder=1 adotta l'ordine SF: cut node prima di alpha. Effetto reale:
      // un cut node con tt_score <= alpha passa da -g_negext_alpha a
      // -g_negext_cut. I nodi PV con tt_score <= alpha restano su alpha.
      // 0 = ordine storico, byte-identico.
      else if (g_negext_tt > 0 && tt_score >= beta) {
        extension = -g_negext_tt;
      }
      // F-018.6d NegExtAlpha (0=off): la TT move non batte nemmeno alpha -> il
      // nodo non e' singolare ne' promettente, riduci l'estensione (Berserk:
      // -1).
      else if (g_negext_order && g_negext_cut > 0 && is_cut_node) {
        extension = -g_negext_cut;
      } else if (g_negext_alpha > 0 && tt_score <= alpha) {
        extension = -g_negext_alpha;
      } else if (g_negext_cut > 0 && is_cut_node) {
        extension = -g_negext_cut;
      }
    }

    // Q-16 RecaptureExt (Caissa Search.cpp:1868, default OFF): ai nodi PV la
    // ttMove che RICATTURA sulla casa dell'ultima mossa avversaria e'
    // quasi-forzata -> +1 ply. Gate strettissimo (PV && ttMove && stessa casa)
    // = costo nodi minimo. Pre-make: l'ultima mossa avversaria e'
    // td.move_stack[td.ply] (vedi history pruning sopra). Filtro TENSIONE
    // (2026-07-12): estende solo se |SEE(ricattura)| <= soglia, cioe' lo
    // scambio e' ~pari. Uno scambio nettamente sbilanciato e' gia' ovvio (SEE
    // alto in un verso o nell'altro) -> l'estensione li' non compra nulla,
    // costa solo nodi.
    if (g_recapture_ext && extension == 0 && pv_node && move == tt_move &&
        is_capture) {
      int prev_rc = td.move_stack[td.ply];
      if (prev_rc && get_move_target(prev_rc) == get_move_target(move)) {
        int see = td_see(td, move);
        if (see >= -g_recapture_tension && see <= g_recapture_tension)
          extension = 1;
      }
    }

    // BrilliantSac detection (6.0, condivisa da v1-estensione e B-reduction-cap):
    // cattura PERDENTE (SEE<0) con capture_history sopra soglia = sacrificio
    // strutturale imparato. Calcolata una volta se un toggle e' attivo; vittima
    // con fallback al pedone avversario (en passant). is_brilliant e' letto anche
    // nel blocco LMR piu' sotto (stesso scope del move loop).
    bool is_brilliant = false;
    if ((g_brilliant_sac || g_brilliant_sac_lmr) && is_capture && !is_promotion &&
        !td_see_at_least(td, move, 0)) {
      int tgt = get_move_target(move);
      int s = (td.side == white) ? p : P;
      int e = (td.side == white) ? k : K;
      int vic = s;
      for (int pc = s; pc <= e; pc++)
        if (get_bit(td.bitboards[pc], tgt)) {
          vic = pc;
          break;
        }
      is_brilliant =
          td.capture_history[get_move_piece(move)][tgt][vic]
                            [td_cbucket(td, tgt)] >= g_brilliant_sac_margin;
    }
    // v1: estende (+1) il sac. NB: questo ramo tocca solo moves_searched==0 (sac
    // ordinato 1o = rarissimo); il caso VERO (sac in fondo) e' gestito da sac_ext
    // nel ramo moves_searched>0 (v1 FIX 2026-07-25). Qui resta per completezza.
    if (g_brilliant_sac && extension == 0 && is_brilliant)
      extension = 1;

    // FIX P0.3 (2026-06-09): catturare lo stato "killer" PRIMA di incrementare
    // td.ply. Il vecchio check nel blocco LMR (`move ==
    // killer_moves[..][td.ply]`) leggeva i killer del ply FIGLIO (mosse
    // dell'avversario: il campo piece differisce sempre) -> non era MAI vero ->
    // lo sconto LMR per i killer era dead code.
    bool is_killer = is_quiet && (move == td.killer_moves[0][td.ply] ||
                                  move == td.killer_moves[1][td.ply]);

    // ThreatHist: bucket-minaccia di QUESTA mossa nel contesto del nodo PADRE
    // (board corrente, PRE-make) -> usato dal read LMR (post-make) e messo
    // nello stack per seo/prior del figlio.
    int move_hbucket = td_hbucket(td, move);

    // Q-15 FollowPV: il figlio "segue la PV" solo se QUESTO nodo la segue e la
    // mossa e' esattamente quella della PV precedente a questo ply. Calcolato
    // PRE-incremento di td.ply (= ply del nodo corrente).
    bool child_follow = g_follow_pv && follow_pv && td.ply < td.prev_pv_len &&
                        move == td.prev_pv[td.ply];

    UndoInfo undo;
    td.seen_stack[td.ply] =
        moves_searched; // F-018.3d: mosse gia' viste a QUESTO nodo (il figlio
                        // le legge su TT-cut)
    td.ply++;
    td.reduction_stack[td.ply] =
        0; // HindsightExt: default 0 (figlio non ridotto); override sotto solo
           // per il reduced-search LMR
    td.de_stack[td.ply] =
        td.de_stack[td.ply - 1] +
        (extension >= 2 ? 1 : 0); // F-018.6b: catena double-ext sul path
    td.repetition_table[++td.repetition_index] = td.hash_key;

    if (!td_make_move(td, move, undo)) {
      td.ply--;
      td.repetition_index--;
      continue;
    }

    legal_moves++;
    if (is_quiet)
      quiets_searched++;

    // "info currmove" a root (convenzione SF: solo main thread, solo analisi
    // lunga > 3s): le GUI mostrano la mossa in esame. Gated dal tempo -> zero
    // costo in gioco/bench (a root il loop gira una manciata di volte).
    if (is_root_node && &td == &thread_data[0] &&
        get_time_ms() - search_start_time > 3000) {
      std::lock_guard<std::mutex> lock(output_mutex);
      printf("info depth %d currmove ", td.root_depth);
      print_move(move);
      printf(" currmovenumber %d\n", legal_moves);
      fflush(stdout);
    }

    // Does this move give check? After make_move the side to move is the
    // opponent, so their king being attacked means our move checks. Checking
    // moves must NOT be reduced/pruned, otherwise forced mates and tactics
    // are found far too late (Stockfish keeps checks at full depth).
    int stm_king_sq =
        get_ls1b_index((td.side == white) ? td.bitboards[K] : td.bitboards[k]);
    bool gives_check = td_is_square_attacked(td, stm_king_sq, td.side ^ 1);
    td.chk_hint[td.ply] = gives_check ? 1 : 0; // letto dal figlio (e dalla sua qsearch)

    // Record the move that leads to the child node (for counter-move).
    td.move_stack[td.ply] = move;
    td.hbucket_stack[td.ply] =
        move_hbucket; // ThreatHist: bucket della mossa entrata in questo ply
                      // (per seo/prior)
    td.captured_stack[td.ply] =
        undo.captured_piece; // victim (-1 if quiet); V2 capture-hist

    // PVS + LMR. Q-11: `nodes_before` serve sia al NodeTM (root) sia al
    // NodeCache (ply<3), quindi lo catturiamo sempre — costa una lettura di
    // td.nodes.
    U64 nodes_before = td.nodes;
    int postlmr_bonus =
        0; // F-018.1: esito della re-search post-LMR (applicato dopo l'unmake)
    if (moves_searched == 0) {
      // 🔴 CutNodeProp (4/08/2026). Qui si passava `false` INCONDIZIONATAMENTE al
      // primo figlio. Stockfish, per il primo figlio di un nodo NON-PV, passa
      // `!cutNode` (Step 18: `search<NonPV>(..., !cutNode)`); solo sulla PV passa
      // `false` (`search<PV>(..., false)`).
      //
      // `false` e' GIUSTO quando il nodo e' CUT, ed e' SBAGLIATO quando e' ALL — e i
      // nodi ALL sono i figli di ogni null move, di ogni probcut e di ogni re-search
      // a profondita' piena. L'etichetta sbagliata si propaga lungo tutta la spina
      // sinistra del sotto-albero non-PV.
      //
      // Cosa consuma `is_cut_node` e cosa si perde col mislabel:
      //   :8004  r += g_lmrf_cut (+g_lmrf_cut_nott)  -> fino a 3,6 ply di riduzione
      //          (5,9 senza TT move). E' il coefficiente PIU' GRANDE di LMRFine.
      //   :8062  scaling ALL-node applicato dove non va
      //   :7716  si perde l'estensione negativa g_negext_cut sulla ttMove
      //   :6686  TTCutRefine rifiuta un cutoff TT valido (is_cut_node != tt_fh)
      //
      // ⚠️ Default 0 = comportamento storico. TUTTI i coefficienti LMRF* sono stati
      // tarati da SPSA CON questo difetto presente: se il fix da solo non rende, non
      // significa che sia sbagliato, significa che quei coefficienti lo avevano
      // assorbito e serve il co-tune del blocco (re-basin, non cancellazione secca).
      const bool child_cut =
        g_cutnode_prop && !pv_node ? !is_cut_node : false;
      score = -td_negamax(td, -beta, -alpha, depth - 1 + extension, child_cut, 0,
                          child_follow);
    } else {
      int reduction = 0;

      // F-005: gate LMR promossi a tunable (erano hardcoded 3/3; SF riduce da
      // depth>=2, 2a mossa).
      if (depth >= g_lmr_min_depth && moves_searched >= g_lmr_min_moves &&
          is_quiet && (!in_check || g_lmr_check_exempt >= 2) &&
          (!gives_check || g_lmr_check_exempt >= 1)) {
        int d_idx = (depth < 63) ? depth : 63;
        int m_idx = (moves_searched < 63) ? moves_searched : 63;
        if (!g_lmr_fine) {
          reduction =
              lmr_table[d_idx]
                       [m_idx]; // assegna alla variabile ESTERNA (niente 'int'
                                // -> niente shadowing che disattivava la LMR)

          if (!pv_node)
            reduction++;
          // ttPv: i nodi non-PV che la TT ricorda come ex-PV vengono ridotti
          // meno (i veri PV node hanno già lo sconto via il !pv_node qui
          // sopra). È il segnale nuovo dello Step ttPv di SF. OFF
          // (g_ttpv=false) = nessun effetto.
          if (g_ttpv_amount > 0 && tt_hit && tt_pv && !pv_node)
            reduction -= g_ttpv_amount;
          if (g_killer_lmr_fix &&
              is_killer) // FIX P0.3: killer del ply del NODO (vedi sopra)
            reduction--;
          if (eval + g_lmr_eval_margin < alpha)
            reduction++;
          if (is_cut_node)
            reduction++;
          // CutNodeLMR (default OFF): SF riduce i cut-node piu' di noi.
          // Aggiunge g_cutnode_lmr_extra ply SOPRA il +1 qui sopra. OFF =
          // byte-identico.
          if (g_cutnode_lmr && is_cut_node)
            reduction += g_cutnode_lmr_extra;
          // LMREnrich (archivio 4.2, default OFF): TT-move noisy + mossa
          // corrente quiet
          // -> riduci di piu' (SF ttCapture). OFF = byte-identico.
          if (g_lmr_enrich && tt_move &&
              (get_move_capture(tt_move) || get_move_enpassant(tt_move) ||
               get_move_promoted(tt_move)))
            reduction += g_lmr_enrich_amount;
          // Enrichment (neutral at default 0): a deep TT entry for this node
          // means the ordering is trustworthy -> reduce a bit less.
          if (g_lmr_ttdepth && tt_hit && tt_depth >= depth)
            reduction -= g_lmr_ttdepth;
          // Reduce one extra ply when the position is NOT improving: a
          // flat/falling eval makes late quiets less likely to be the move.
          if (g_improving && !improving)
            reduction++;

          // History-based reduction. DEFAULT (StatScoreLMR off): butterfly
          // history clamped to +/-1 ply (conservativo). StatScoreLMR on: stile
          // SF, riduzione CONTINUA (niente clamp) -> quiet con ottima history
          // ridotti molti ply in piu', quelli con pessima history molti in meno
          // = il fix della SOTTO-RIDUZIONE (gap vs SF15.1). Divisore
          // SPSA-tunable.
          int raw_hist = td.history_moves[move_hbucket][get_move_piece(move)]
                                         [get_move_target(move)];
          if (g_statscore_lmr) {
            int ss = 2 * raw_hist - g_lmr_ss_offset;
            reduction -= ss / g_lmr_ss_div;
          } else {
            int hist_r, hclamp;
            if (g_aggr_lmr) {
              hist_r = raw_hist / g_aggr_lmr_div;
              hclamp = g_aggr_lmr_clamp;
            } else {
              hist_r = raw_hist / g_hist_red_div;
              hclamp = 1;
            }
            if (hist_r > hclamp)
              hist_r = hclamp;
            else if (hist_r < -hclamp)
              hist_r = -hclamp;
            reduction -= hist_r;
          }

          // Continuation-history reduction. ContHistLMR on: conthist CONTINUO a
          // 1/2/4 ply indietro (scollegato da ContHistPrune) = il segnale che
          // SF usa pesantemente nella LMR e noi ignoravamo di default.
          // Altrimenti il legacy +/-1 ch_r (solo se ContHistPrune on). NB:
          // td.ply gia' incrementato per il figlio -> mossa precedente a
          // move_stack[td.ply-1].
          if (g_conthist_lmr) {
            int prev = td.move_stack[td.ply - 1];
            int ch = 0;
            if (prev)
              ch +=
                  td.continuation_history[get_move_piece(prev)][get_move_target(
                      prev)][get_move_piece(move)][get_move_target(move)];
            if (td.ply >= 2) {
              int p2 = td.move_stack[td.ply - 2];
              if (p2)
                ch +=
                    td.cont_hist_2[get_move_piece(p2)][get_move_target(p2)]
                                  [get_move_piece(move)][get_move_target(move)];
            }
            if (td.ply >= 4) {
              int p4 = td.move_stack[td.ply - 4];
              if (p4)
                ch +=
                    td.cont_hist_4[get_move_piece(p4)][get_move_target(p4)]
                                  [get_move_piece(move)][get_move_target(move)];
            }
            reduction -= ch / g_lmr_ch_div;
          } else if (g_cont_hist_prune) {
            int prev = td.move_stack[td.ply - 1];
            if (prev) {
              int ch =
                  td.continuation_history[get_move_piece(prev)][get_move_target(
                      prev)][get_move_piece(move)][get_move_target(move)];
              int ch_r, cclamp;
              if (g_aggr_lmr) {
                ch_r = ch / g_aggr_lmr_div;
                cclamp = g_aggr_lmr_clamp;
              } else {
                ch_r = ch / g_conthist_red_div;
                cclamp = 1;
              }
              if (ch_r > cclamp)
                ch_r = cclamp;
              else if (ch_r < -cclamp)
                ch_r = -cclamp;
              reduction -= ch_r;
            }
          }

          // DiverseSMP: nudge this helper's reduction to diversify its tree.
          // The clamps below keep it in [0, depth-2], so the bias can't break
          // LMR.
          if (g_diverse_smp)
            reduction += td.lmr_bias;

          // cutoffCnt-LMR (SF, default off): qui td.ply == ply del FIGLIO (post
          // ply++), quindi td.cutoff_cnt[td.ply] e' il (ss+1)->cutoffCnt di SF.
          // Se il figlio ha cuttato molto di recente, riduci di piu'.
          if (g_cutoffcnt_penalty && td.cutoff_cnt[td.ply] > 3)
            reduction += g_cutoffcnt_penalty;

          // CorrValExt: eval pesantemente corretta = incerta -> riduci di MENO
          // (come SF/Reckless).
          if (g_corrval_ext)
            reduction -=
                (corr_val < 0 ? -corr_val : corr_val) * g_corrval_lmr / 4096;
        } else {
          // ⭐ Riduzione FINE stile-SF in 1/1024 di ply (port
          // search.cpp:1287-1334): blenda i segnali con peso fine e taglia
          // FORTE solo dove e' sicuro. r in millesimi di ply.
          long long r = g_lmr_sf_base ? (long long)lmr_sf_table[d_idx][m_idx]
                                      : (long long)lmr_table[d_idx][m_idx] *
                        1024; // base dalla nostra tabella (o da quella alla SF, vedi LMRSFBase)
          r += lmr_deep_table[d_idx][m_idx]; // LMRDeepK: 0 sotto d0 o con k = 0
          if (tt_pv && !pv_node)
            r -= g_lmrf_ttpv; // protezione ex-PV
          if (pv_node)
            r -= g_lmrf_pv; // protezione PV
          r -= (long long)(corr_val < 0 ? -corr_val : corr_val) * g_lmrf_corr /
               4096; // eval incerta -> meno
          if (is_cut_node)
            r += g_lmrf_cut + (tt_move == 0
                                   ? g_lmrf_cut_nott
                                   : 0); // CUT-NODE forte (il divario #1)
          if (tt_move &&
              (get_move_capture(tt_move) || get_move_enpassant(tt_move) ||
               get_move_promoted(tt_move)))
            r += g_lmrf_ttcap;
          // statScore continuo (2*main + cont1 + cont2), come SF
          int sscore = 2 * td.history_moves[move_hbucket][get_move_piece(move)]
                                           [get_move_target(move)];
          {
            int prv = td.move_stack[td.ply - 1];
            if (prv)
              sscore +=
                  td.continuation_history[get_move_piece(prv)][get_move_target(
                      prv)][get_move_piece(move)][get_move_target(move)];
            if (td.ply >= 2) {
              int p2 = td.move_stack[td.ply - 2];
              if (p2)
                sscore +=
                    td.cont_hist_2[get_move_piece(p2)][get_move_target(p2)]
                                  [get_move_piece(move)][get_move_target(move)];
            }
          }
          r -= (long long)sscore * g_lmrf_ss / 4096;
          // ContHist4LMR: la conthist a 4 ply, che il ramo legacy usava e il
          // port di LMRFine ha lasciato fuori (vedi il commento alla
          // dichiarazione). Termine a se' stante: 0 = byte-identico. Stessa
          // convenzione di indice del resto del blocco (td.ply e' gia'
          // incrementato per il figlio).
          if (g_lmrf_cont4 && td.ply >= 4) {
            int p4 = td.move_stack[td.ply - 4];
            if (p4)
              r -=
                  (long long)td.cont_hist_4[get_move_piece(p4)][get_move_target(
                      p4)][get_move_piece(move)][get_move_target(move)] *
                  g_lmrf_cont4 / 4096;
          }
          if (g_improving && !improving)
            r += g_lmrf_improv;
          if (eval + g_lmr_eval_margin < alpha)
            r += g_lmrf_evalcut;
          // LmrAlphaGap (SF 5f7348f0): la rampa continua sullo stesso segnale
          // del gradino qui sopra. Stesse unita' (1/1024 di ply) del resto del
          // blocco, che e' gia' un port di search.cpp.
          // Il `!capture` di SF e' gia' garantito: questo ramo sta sotto il gate
          // `is_quiet` (:8792). Resta da replicare `!is_decisive(alpha)`, che da
          // noi e' |alpha| >= mate_score - max_ply (= 29936 = TB_VALUE_WIN, la
          // stessa soglia usata dalla banda tablebase di td_corr_update).
          if (g_lmr_alpha_gap &&
              (alpha < 0 ? -alpha : alpha) < mate_score - max_ply) {
            int gap = alpha - eval;
            if (gap < -g_lmr_alpha_lo)
              gap = -g_lmr_alpha_lo;
            else if (gap > g_lmr_alpha_hi)
              gap = g_lmr_alpha_hi;
            r += (long long)g_lmr_alpha_gap * gap;
          }
          if (td.cutoff_cnt[td.ply] > 1) {
            r += g_lmrf_cutoff;
            // LMRExpect (Reckless 0efe38e, STC +3.08 / LTC +4.13): "aspettativa
            // del nodo RISPETTATA". A un ALL-node (ne' PV ne' cut) ci si
            // aspetta che tutto fallisca basso; se per giunta il figlio ha
            // cuttato molto, l'aspettativa e' confermata
            // -> si puo' ridurre ancora di piu'. E' l'INTERAZIONE fra i due
            // segnali che gia' abbiamo separati (g_lmrf_cutoff e g_lmrf_all),
            // mai combinati. Stesse unita' di Reckless (1/1024 di ply): loro
            // +256/+384. 0 = byte-identico.
            if (g_lmr_expect && !pv_node && !is_cut_node)
              r += g_lmr_expect;
          }
          // scaling ALL-node (ne' PV ne' cut): taglia di piu' dove tutto
          // fallisce-basso
          if (!pv_node && !is_cut_node)
            r += r * g_lmrf_all / (256LL * depth + 285);
          // Q-19a (Caissa): termine di PLY — riduci MENO vicino alla radice.
          // LMRFine non aveva NESSUN termine di ply. 0 = off.  r -=
          // ply_coef*depth/(1+ply+depth)
          if (g_lmrf_ply && pv_node)
            r -= (long long)g_lmrf_ply * depth / (1 + td.ply + depth);
          // Q-19b (Caissa): killer/counter ridotte sensibilmente meno (in
          // millesimi, additivo all'intero `reduction--` di KillerLMRFix
          // sotto). 0 = off.
          if (g_lmrf_killer && is_killer)
            r -= g_lmrf_killer;
          if (g_diverse_smp)
            r += (long long)
                     td.lmr_bias_units; // = lmr_bias*1024 se DiverseSMPFine=0
          reduction = (int)(r / 1024);
          // protezioni intere mantenute (piccole)
          if (g_killer_lmr_fix && is_killer)
            reduction--;
          if (g_lmr_ttdepth && tt_hit && tt_depth >= depth)
            reduction -= g_lmr_ttdepth;
        }

        // Q-30 (Reckless ae986df, default OFF): beta gia' decisivo (stiamo
        // provando un matto) -> riduci di piu' le tardive. Non-regressione +
        // matefinding migliore per loro (+28 matti su matetrack). Vale per
        // entrambi i rami.
        if (g_lmr_decisive_beta && beta >= mate_score)
          reduction++;

        // 🔴 LMRNegative (4/08/2026). Qui la riduzione veniva troncata a 0: la nostra
        // LMR non puo' MAI estendere. Stockfish e Hobbes permettono r negativa
        // (`reduced_depth = clamp(new_depth - r, 1, new_depth + 1 + (legal<=3))`),
        // cioe' cercano PIU' A FONDO le mosse molto ben ordinate.
        //
        // Il costo del troncamento non e' solo l'estensione mancata: tutti i termini
        // "riduci meno" di LMRFine — killer 797, ply 524, corr 866, ttpv, pv, ss —
        // appena sommano sotto zero finiscono sullo STESSO valore e diventano
        // indistinguibili fra loro. Sei parametri tarati che sotto quella soglia
        // fanno tutti esattamente la stessa cosa: niente.
        //
        // Default 0 = comportamento storico. Con la riduzione negativa
        // `reduced_depth > full_depth`, quindi la re-search a profondita' piena viene
        // saltata dal gate `full_depth > reduced_depth`: corretto, l'abbiamo gia'
        // cercata piu' a fondo.
        const int red_floor =
            g_lmr_negative ? -(1 + (moves_searched <= 3 ? 1 : 0)) : 0;
        if (reduction < red_floor)
          reduction = red_floor;
        if (reduction > depth - 2)
          reduction = depth - 2;
      }
      // F-004 (audit 2026-07-02): LMR anche sulle CATTURE (SF/Stormphrax
      // riducono le noisy tardive; noi mai = gap strutturale). Riduzione dalla
      // stessa lmr_table scalata % (default 50 = dimezzata), MAI su
      // promozioni/scacchi, con esenzione RICATTURA sulla casa appena mossa
      // dall'avversario (stessa esenzione della QFutility: potare/ridurre il
      // recupero = perdere la valutazione dello scambio). Default OFF =
      // byte-identico.
      else if (g_lmr_captures && is_capture && !is_promotion &&
               (!in_check || g_lmr_check_exempt >= 2) &&
               (!gives_check || g_lmr_check_exempt >= 1) &&
               depth >= g_lmr_min_depth &&
               moves_searched >= g_lmr_min_moves) {
        int prev_mv = td.move_stack[td.ply - 1];
        int prev_sq = prev_mv ? get_move_target(prev_mv) : -1;
        if (get_move_target(move) != prev_sq) {
          int d_idx = (depth < 63) ? depth : 63;
          int m_idx = (moves_searched < 63) ? moves_searched : 63;
          reduction = lmr_table[d_idx][m_idx] * g_lmr_cap_scale / 100;
          if (!pv_node)
            reduction++;
          if (reduction < 0)
            reduction = 0;
          if (reduction > depth - 2)
            reduction = depth - 2;
        }
      }

      // BrilliantSac-B (reduction-cap, default OFF): il sac strutturale ordinato
      // tardi qui verrebbe ridotto -> risparmia N plies di riduzione (reduce-less),
      // dove v1 (estensione della 1a mossa) non arriva. is_brilliant true solo per
      // catture -> il path quiet non e' toccato.
      if (g_brilliant_sac_lmr && is_brilliant && reduction > 0) {
        reduction -= g_brilliant_sac_lmr_amt;
        if (reduction < 0)
          reduction = 0;
      }

      // v1 FIX (2026-07-25): un sac (SEE<0) e' ordinato in fondo -> NON e' mai
      // moves_searched==0, quindi l'`extension=1` di v1 (consumata solo nel ramo
      // della 1a mossa) era codice MORTO sul suo bersaglio. Qui, nel ramo
      // moves_searched>0 dove il sac arriva davvero, applichiamo il +1 alle
      // profondita' LMR/re-search: ora v1 estende sul serio. Default OFF -> 0.
      int sac_ext = (g_brilliant_sac && is_brilliant) ? 1 : 0;
      // Reduced-depth zero-window search (LMR). reduced_depth = depth-1-r.
      int reduced_depth = depth - 1 - reduction + sac_ext;
      td.reduction_stack[td.ply] =
          reduction; // HindsightExt: il figlio sa di essere stato ridotto di
                     // `reduction` plies
      score = -td_negamax(td, -alpha - 1, -alpha, reduced_depth, true, 0,
                          child_follow);

      // Full-depth re-search when the reduced search fails high.
      int full_depth = depth - 1 + sac_ext;
      if (score > alpha && reduction > 0) {
        td.reduction_stack[td.ply] =
            0; // HindsightExt: la re-search e' a profondita' PIENA ->
               // reduction=0 per il figlio
        // DoDeeper/DoShallower (SF :doDeeperSearch): se la re-search batte il
        // best di molto cerca piu' a fondo (+1), se lo batte di poco piu' a
        // corto (-1). best_score qui = best delle mosse PRECEDENTI (running
        // best, come SF bestValue).
        if (g_do_deeper) {
          bool do_deeper =
              score > best_score + g_dodeeper_base + 2 * full_depth;
          bool do_shallower = score < best_score + g_doshallower_margin;
          full_depth += (int)do_deeper - (int)do_shallower;
        }
        if (full_depth > reduced_depth) {
          score = -td_negamax(td, -alpha - 1, -alpha, full_depth, !is_cut_node,
                              0, child_follow);
          // F-018.1 PostLMRHist (default OFF): la re-search full-depth VERIFICA
          // la riduzione — se conferma il fail-high la mossa merita bonus
          // conthist, se smentisce merita malus (mainline SF, presente in
          // Obsidian e Berserk).
          if (g_postlmr_hist)
            postlmr_bonus = (score <= alpha  ? -td_stat_bonus(full_depth)
                             : score >= beta ? td_stat_bonus(full_depth)
                                             : 0) *
                            g_postlmr_scale / 100;
        }
      }

      // PV full-window re-search (uses the possibly-adjusted full_depth so a
      // doDeeper bump also deepens the PV confirmation, matching SF).
      if (score > alpha && score < beta) {
        score =
            -td_negamax(td, -beta, -alpha, full_depth, false, 0, child_follow);
      }
    }

    td.chk_hint[td.ply] = -1; // fine della vita del figlio: il suggerimento non vale piu'
    td_unmake_move(td, move, undo);
    td.ply--;
    td.repetition_index--;

    if (stop_threads.load(std::memory_order_relaxed))
      return 0;

    // Q-11 NodeCache (Caissa Search.cpp:2024-2029): registra i nodi spesi sotto
    // questa mossa. Dopo lo stop-check: una ricerca abortita darebbe un
    // conteggio troncato che inquinerebbe l'ordering delle iterazioni
    // successive.
    if (nc)
      nc_add(nc, move, td.nodes - nodes_before);

    // F-018.1 PostLMRHist: update conthist DOPO l'unmake (convenzioni ply del
    // nodo, come il blocco fail-high sotto). Su ogni tipo di mossa, come SF.
    if (postlmr_bonus) {
      int plh_prev = td.move_stack[td.ply];
      if (plh_prev)
        td_update_history(
            td.continuation_history[get_move_piece(plh_prev)][get_move_target(
                plh_prev)][get_move_piece(move)][get_move_target(move)],
            postlmr_bonus);
      td_conthist_multi_update(td, move, postlmr_bonus);
    }

    moves_searched++;
    if (is_quiet && n_searched_quiets < 64)
      searched_quiets[n_searched_quiets++] = move;
    else if (is_capture && n_searched_captures < 64)
      searched_captures[n_searched_captures++] = move;

    if (score > best_score) {
      best_score = score;
      best_move = move;
      // Node-based TM: record how many nodes this (new best) root move cost.
      if (is_root_node)
        td.root_bestmove_nodes = td.nodes - nodes_before;

      if (score > alpha) {
        alpha = score;
        hash_flag = hash_flag_exact;

        td.pv_table[td.ply][td.ply] = move;
        for (int i = td.ply + 1; i < td.pv_length[td.ply + 1]; i++)
          td.pv_table[td.ply][i] = td.pv_table[td.ply + 1][i];
        td.pv_length[td.ply] = td.pv_length[td.ply + 1];

        // F-018.9 AlphaDepthDec (default OFF): trovato un best "vero" (alpha
        // alzato, niente cutoff), le mosse RESTANTI meritano meno sforzo ->
        // depth-1 per il resto del loop (Berserk/SF; il TT store finale usa la
        // depth ridotta).
        if (g_alpha_depth_dec && score < beta && score > -mate_score &&
            score < mate_score && depth <= 11) {
          if (g_alpha_depth_dec_amt <= 1) {
            if (depth >= 2)
              depth--;
          } else if (depth > g_alpha_depth_dec_amt)
            depth -= g_alpha_depth_dec_amt; // SF: depth -= 3 per 3 < depth < 12
        }

        if (score >= beta) {
          // CutoffStats (diagnostica, off-default): fail-high node.
          // moves_searched e' GIA' incrementato (riga sopra) -> cutoff sulla 1a
          // mossa <=> ==1.
          if (g_cutoff_stats) {
            td.fh_nodes++;
            td.fh_move_sum += (U64)moves_searched;
            if (moves_searched == 1)
              td.fh_first++;
            if (tt_move) {
              td.fh_tt++;
              if (moves_searched == 1)
                td.fh_tt_first++;
            }
            if (tt_hit)
              td.fh_probe++; // entry TT presente (anche senza mossa)
          }
          // FHBlend (P2): prima dello store TT e dell'update di correzione, come SF.
          // Soglia "decisivo" = banda TB inclusa (mate_score - 2*max_ply, cfr. CorrTBGuard).
          if (g_fh_blend && best_score < mate_score - 2 * max_ply &&
              beta < mate_score - 2 * max_ply && beta > -(mate_score - 2 * max_ply))
            best_score = (best_score * depth + beta) / (depth + 1);
          // PrevRefuteMalus (P3): la quiet del padre, prima mossa non-TT cercata dal
          // padre, e' stata confutata subito -> malus alla sua conthist (1 ply).
          // seen_stack[ply-1] = mosse gia' cercate dal padre prima di questa.
          if (g_prev_refute_malus && td.ply >= 2) {
            const int pm = td.move_stack[td.ply];
            const int ptt = td.ttmove_stack[td.ply - 1];
            if (pm && pm != ptt && td.captured_stack[td.ply] == -1 &&
                !get_move_promoted(pm) &&
                td.seen_stack[td.ply - 1] == (ptt ? 1 : 0)) {
              const int pm2 = td.move_stack[td.ply - 1];
              if (pm2)
                td_update_history(
                    td.continuation_history[get_move_piece(pm2)][get_move_target(pm2)]
                                           [get_move_piece(pm)][get_move_target(pm)],
                    -td_stat_bonus(depth) * g_prev_refute_malus / 100);
            }
          }
          td.cutoff_cnt[td.ply] +=
              tt_move ? 1 : 2; // SF cutoffCnt: += 1 + !ttMove
          if (!excluded_move && !(is_root_node && td.root_only_move))
            store_tt(td.hash_key, move, best_score, depth, hash_flag_beta,
                     td.ply, store_pv,
                     tt_store_eval(node_raw_eval, tt_eval, tt_flag, td.fifty));
          td_corr_update(td, corr_idx, static_eval, best_score, hash_flag_beta,
                         depth, in_check, move, excluded_move);

          if (is_quiet) {
            // Evita di clonare la stessa mossa in entrambi gli slot
            // killer (dimezzerebbe le chance di cutoff).
            if (move != td.killer_moves[0][td.ply]) {
              td.killer_moves[1][td.ply] = td.killer_moves[0][td.ply];
              td.killer_moves[0][td.ply] = move;
            }
            int prev_cm = td.move_stack[td.ply];
            int pcp = prev_cm ? get_move_piece(prev_cm) : 0;
            int pct = prev_cm ? get_move_target(prev_cm) : 0;
            if (g_countermove && prev_cm)
              td.counter_moves[pcp][pct] = move;
            // F-018.10a FHBoostMargin (0=off): fail-high LARGO = segnale piu'
            // forte
            // -> bonus/malus calcolati a depth+1 (Obsidian +95, Berserk +75).
            int bonus =
                td_stat_bonus(depth + ((g_fhboost_margin &&
                                        best_score > beta + g_fhboost_margin)
                                           ? 1
                                           : 0));
            // Q-18 (SF PR #6871, gainer): scala il bonus sul numero di mosse
            // cercate PRIMA della best — un cutoff tardivo e' un segnale piu'
            // informativo di uno alla prima mossa. 0 = off (bonus piatto,
            // comportamento storico).
            bonus = td_hist_bonus_moves(bonus, moves_searched);
            // PawnHistory: indice dal board corrente (= board del nodo qui,
            // la cutoff-move e' gia' stata unmade -> corretto). Locale = no
            // staleness da ricorsione.
            int pawn_pk = g_pawn_hist
                              ? (td_corr_index(td) & ThreadData::PAWN_HIST_MASK)
                              : 0;
            // F-018.10b HistTrivGuard (default OFF): cutoff "gratis" (depth
            // bassa, nessun quiet provato prima) -> niente bonus: gonfierebbe
            // mosse che hanno tagliato senza concorrenza
            // (Obsidian/Berserk/Ethereal).
            bool triv_cut =
                g_hist_triv_guard && depth <= 3 && n_searched_quiets <= 1;
            if (!triv_cut) {
              td_update_history(td.history_moves[move_hbucket][get_move_piece(
                                    move)][get_move_target(move)],
                                bonus);
              // Q-12 CMHC: il bonus CONTHIST (non la main-history) e' scalato
              // per la consistenza della mossa nei contesti conthist. 0 =
              // neutro.
              int chbonus = bonus * td_cmhc_factor(td, move) / 100;
              if (prev_cm)
                td_update_history(
                    td.continuation_history[pcp][pct][get_move_piece(move)]
                                           [get_move_target(move)],
                    chbonus);
              td_conthist_multi_update(td, move, chbonus);
              if (g_pawn_hist)
                td_update_history(td.pawn_history[pawn_pk][get_move_piece(move)]
                                                 [get_move_target(move)],
                                  bonus);
              if (g_lowply && td.ply < ThreadData::LOW_PLY_MAX)
                td_update_history(td.lowply_history[td.ply][get_move_piece(
                                      move)][get_move_target(move)],
                                  bonus);
            }
            // malus: quiet moves tried before the cutoff get penalized
            for (int q = 0; q < n_searched_quiets; q++) {
              int qm = searched_quiets[q];
              if (qm == move)
                continue;
              // MalusScaled (default OFF -> qmalus=bonus=piatto): le quiet
              // TARDIVE (q grande) ricevono malus MINORE (come Reckless): denom
              // = 1024 + coef*q. F-018.10c MalusPct (100=identico al bonus):
              // costanti malus separate.
              int qmalus;
              if (g_malus_quad) { // caduta quadratica (Reckless 5e669a6)
                int denom = 1024 + g_malus_quad_coef * q;
                qmalus = bonus * 1024 / denom;
                qmalus = qmalus * 1024 /
                         denom; // in due passi: bonus*1024*1024 sfonda int32
              } else
                qmalus = g_malus_scaled
                             ? bonus * 1024 / (1024 + g_malus_scale_coef * q)
                             : bonus;
              qmalus = qmalus * g_malus_pct / 100;
              td_update_history(
                  td.history_moves[td_hbucket(td, qm)][get_move_piece(qm)]
                                  [get_move_target(qm)],
                  -qmalus);
              if (prev_cm)
                td_update_history(
                    td.continuation_history[pcp][pct][get_move_piece(qm)]
                                           [get_move_target(qm)],
                    -qmalus);
              td_conthist_multi_update(td, qm, -qmalus);
              if (g_pawn_hist)
                td_update_history(td.pawn_history[pawn_pk][get_move_piece(qm)]
                                                 [get_move_target(qm)],
                                  -qmalus);
              if (g_lowply && td.ply < ThreadData::LOW_PLY_MAX)
                td_update_history(td.lowply_history[td.ply][get_move_piece(qm)]
                                                   [get_move_target(qm)],
                                  -qmalus);
            }
          } else if (is_capture && g_capture_hist) {
            // Capture history: BONUS alla cattura che ha causato il cutoff.
            int bonus = td_hist_bonus_moves(td_stat_bonus(depth),
                                            moves_searched); // Q-18
            int vic = td_captured_piece(td, get_move_target(move));
            td_update_history(
                td.capture_history[get_move_piece(move)][get_move_target(move)]
                                  [vic][td_cbucket(td, get_move_target(move))],
                bonus);
          }

          // SF-faithful: il MALUS alle catture provate-e-fallite si applica
          // a OGNI beta-cutoff (anche quando la best move e' QUIET), non solo
          // quando il cutoff e' causato da una cattura. Senza questo, le
          // catture speculative che falliscono prima di un cutoff quiet non
          // accumulano mai segnale negativo -> capture_history resta
          // netto-positiva -> vengono sovra-ordinate -> -Elo. (Era il bug che
          // rendeva la feature negativa: -21 @div1, -12 @div16.)
          if (g_capture_hist) {
            int cap_malus =
                td_stat_bonus(depth) * g_malus_pct / 100; // F-018.10c
            for (int c = 0; c < n_searched_captures; c++) {
              int cm = searched_captures[c];
              if (cm == move)
                continue; // la cattura-cutoff ha gia' il bonus
              int cvic = td_captured_piece(td, get_move_target(cm));
              td_update_history(
                  td.capture_history[get_move_piece(cm)][get_move_target(cm)]
                                    [cvic][td_cbucket(td, get_move_target(cm))],
                  -cap_malus);
            }
          }

          return best_score;
        }
      }
    }
  }

  // Checkmate / Stalemate
  if (legal_moves == 0) {
    if (in_check)
      return -mate_value + td.ply;
    return 0;
  }

  // V2 (PriorBonus, default OFF -> byte-identico): nodo FAIL-LOW (hash_flag
  // ancora alpha = nessuna mossa ha battuto alpha) -> la mossa PRECEDENTE
  // (quella che ci ha portato qui) era forte -> bonus alla sua history. Quiet:
  // main + 1-ply continuation; cattura: capture-hist (vittima da
  // captured_stack, mv1 e' gia' stata fatta = via dal board).
  if (g_prior_bonus && hash_flag == hash_flag_alpha &&
      (!g_prior_bonus_gate || is_cut_node || pv_node)) {
    int mv1 = td.move_stack[td.ply];
    if (mv1) {
      int p1 = get_move_piece(mv1), t1 = get_move_target(mv1);
      int pbonus = g_prior_bonus_scale * td_stat_bonus(depth) / 100;
      if (g_prior_bonus_factor && td.ply >= 1) {
        int mc = td.seen_stack[td.ply - 1];
        int mc_term = g_pb_mc * mc;
        if (mc_term > g_pb_mc_cap)
          mc_term = g_pb_mc_cap;
        int pev = td.eval_stack[td.ply - 1];
        int factor =
            g_pb_base + mc_term +
            (mv1 == td.ttmove_stack[td.ply - 1] ? g_pb_ttm : 0) +
            (!in_check && best_score <= static_eval - g_pb_ev_marg ? g_pb_ev
                                                                   : 0) +
            (pev != EVAL_NONE && best_score <= -pev - g_pb_swing_marg
                 ? g_pb_swing
                 : 0);
        pbonus = factor * g_pb_scale * td_stat_bonus(depth) / (100 * 128);
      }
      if (!get_move_capture(mv1)) {
        td_update_history(td.history_moves[td.hbucket_stack[td.ply]][p1][t1],
                          pbonus);
        if (td.ply >= 1) {
          int mv2 = td.move_stack[td.ply - 1];
          if (mv2)
            td_update_history(td.continuation_history[get_move_piece(
                                  mv2)][get_move_target(mv2)][p1][t1],
                              pbonus);
        }
      } else if (g_capture_hist) {
        int vic = td.captured_stack[td.ply];
        // CapHistThreat: bucket 0 FISSO qui. Questo e' il PriorBonus, che premia
        // la cattura del nodo PADRE: le minacce andrebbero lette sulla board del
        // padre, non su questa. ponytail: se la feature paga, la strada e' un
        // cbucket_stack come l'hbucket_stack che il lato quiet usa per lo stesso
        // motivo — non vale scriverlo prima di sapere se serve.
        if (vic >= 0)
          td_update_history(td.capture_history[p1][t1][vic][0], pbonus);
      }
    }
  }

  td_corr_update(td, corr_idx, static_eval, best_score, hash_flag, depth,
                 in_check, best_move, excluded_move);
  // TTPvInherit: il fail-low eredita il ttPv del padre (SF step 23).
  if (g_ttpv_inherit && hash_flag == hash_flag_alpha && td.ply >= 1 &&
      td.ttpv_stack[td.ply - 1])
    store_pv = true;
  // root_only_move (TroubleMaking): la verifica di UNA mossa non deve
  // lasciare alla root un'entry con quella mossa come best (flag alpha, depth
  // ridotta): l'iterazione successiva la ordinerebbe per prima e la cercherebbe
  // a finestra piena al posto della best vera.
  if (!excluded_move && !(is_root_node && td.root_only_move))
    store_tt(td.hash_key,
             (hash_flag == hash_flag_alpha && !g_tt_faillow_move) ? 0 : best_move,
             best_score, depth, hash_flag, td.ply, store_pv,
             tt_store_eval(node_raw_eval, tt_eval, tt_flag, td.fifty));

  return best_score;
}
