// search/09_history.inc -- Valutazione statica, statistiche delle mosse, correzione della valutazione.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

// ============================================================================
// VALUTAZIONE
// ============================================================================

// Comando "eval": valutazione statica della scacchiera globale (lato al tratto) e uscita grezza della rete. La rete
// legge i nostri bitboard cosi' come sono (07/10/2026, scacchiera unica v2).
int debug_eval_position() {
  return nn_eval(side == white, bitboards, occupancies, fifty);
}
int debug_eval_position_raw() {
  int raw = 0;
  nn_eval(side == white, bitboards, occupancies, fifty, &raw);
  return raw;
}

// Riparte dalla radice con la catena degli accumulatori della rete (a ogni iterazione). La rete legge la scacchiera
// del thread direttamente: non c'e' una sua copia da riallineare, solo la pila delle dirty da azzerare.
static inline void nn_root_sync(ThreadData &td) {
  nn_pos_set(td.nnpos);
#ifdef TRIUMV_VERIFY_NNSYNC
  td.nnstack->st[0].key = nn_piece_key(td);
#endif
}

// La rete valuta la scacchiera del thread. Prima: minacce e pedoni che mancano (nn_dirty_catch_up) e, con
// -DTRIUMV_VERIFY_NNSYNC, il controllo che la pila descriva questa scacchiera (06_nndirty.inc).
static inline int td_nn_eval(ThreadData &td) {
  {
    PROF_GUARD(prof_catchup);
    nn_dirty_catch_up(td);
  }
  nn_verify_sync(td);
  return nn_pos_eval(td.nnpos, td.bitboards, td.occupancies, td.piece_on, td.side == white, td.fifty);
}

// La rete smorza la valutazione col contatore delle 50 mosse: v * (199 - fifty) / 199. In TT e nella cache si
// tiene il valore SENZA smorzamento, cosi' vale per ogni trasposizione, e lo si rismorza alla lettura.
// W1 (08/10/2026, velocita', stesso valore): il divisore 199 - fifty cambia a run time e la divisione era una idiv
// (~26 cicli di latenza) a ogni valutazione fresca, due volte (cache delle valutazioni e hash). Ora moltiplica per il
// reciproco M = ceil(2^40 / D) e tronca verso zero: esatto finche' |x| * (M*D - 2^40) < 2^40, cioe' |x| < 2^40 / 199;
// qui |x| = |v| * 199 < 2^28 (verificato con numpy per ogni fifty e |v| <= 2^20: 0 differenze). xperf 6 giri, PGO:
// cicli -0,42% mediogioco, +0,19% finali (nel rumore); tenuta perche' l'albero e' identico (docs/audit_8.0/W_*).
static constexpr auto unfade_recip = [] {
  std::array<unsigned long long, 101> r{};
  for (int f = 0; f <= 100; f++)
    r[f] = ((1ULL << 40) + (unsigned long long)(198 - f)) / (unsigned long long)(199 - f);
  return r;
}();
static inline int eval_unfade(int v, int fifty) {
  fifty = std::min(fifty, 100);
  const long long x = (long long)v * 199;
  const unsigned long long mag = (unsigned long long)(x < 0 ? -x : x);
  const int q = (int)((mag * unfade_recip[fifty]) >> 40);
  return x < 0 ? -q : q;
}
static inline int eval_refade(int v, int fifty) {
  if (fifty > 100) fifty = 100;
  // Lo stesso troncamento del ponte (nn_scale, nn_finalize: v - v*f/199), cosi' la stessa posizione vale uguale
  // fresca, dalla cache o dalla hash (05/10/2026, audit del nucleo; BAKE PULIZIA 06/10, opzione EvalRefadeBridge
  // ritirata la sera; prima v * (199 - fifty) / 199).
  return v - v * fifty / 199;
}

// Esperto della rete (fascia di materiale) per numero di pezzi: la stessa tabella di half_ka_v2_hm.h
// (<= 9, 10-15, 16-23, >= 24). Serve a PhaseEdge* e CorrPhase.
static inline int net_phase(int pieces) { return pieces <= 9 ? 0 : pieces <= 15 ? 1 : pieces <= 23 ? 2 : 3; }

// Valutazione della rete (unita' della rete), con la cache per thread.
static inline int net_evaluate(ThreadData &td) {
  PROF_GUARD(prof_eval);
  if (g_eval_off) {
    static const int mat[6] = {100, 320, 330, 500, 900, 0};
    int s = 0;
    for (int pc = 0; pc < 6; pc++)
      s += mat[pc] * (count_bits(td.bitboards[pc]) - count_bits(td.bitboards[pc + 6]));
    return td.side == white ? s : -s;
  }
  ThreadData::EvalCacheEntry &ce = td.eval_cache[td.hash_key & ThreadData::EVAL_CACHE_MASK];
  if (ce.key == td.hash_key)
    return eval_refade(ce.eval, td.fifty);
  const int v = td_nn_eval(td);
  ce.key = td.hash_key;
  ce.eval = eval_unfade(v, td.fifty);
  return v;
}

// Termini dal residuo (Res*, 01_params.inc, 09/10/2026): concetti che la rete sottostima rispetto alla ricerca
// profonda. Case a8 = 0 .. h1 = 63 (riga 0 = ottava traversa), bitboard P N B R Q K p n b r q k. Restituisce
// centipedoni stampati, bianco meno nero.
struct ResTables {
  U64 ahead_adj[2][64];   // case davanti al pedone sulla sua colonna e sulle vicine (passato se nessun pedone nemico)
  U64 file[8];
  ResTables() {
    for (int f = 0; f < 8; f++) {
      file[f] = 0;
      for (int r = 0; r < 8; r++) file[f] |= 1ULL << (r * 8 + f);
    }
    for (int sq = 0; sq < 64; sq++) {
      const int row = sq >> 3, f = sq & 7;
      ahead_adj[white][sq] = ahead_adj[black][sq] = 0;
      for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ff++)
        for (int rr = 0; rr < 8; rr++) {
          if (rr < row) ahead_adj[white][sq] |= 1ULL << (rr * 8 + ff);
          if (rr > row) ahead_adj[black][sq] |= 1ULL << (rr * 8 + ff);
        }
    }
  }
};

static int residual_terms_cp(const ThreadData &td) {
  static const ResTables T;
  constexpr U64 FILE_A = 0x0101010101010101ULL, FILE_H = 0x8080808080808080ULL;
  const int pieces = count_bits(td.occupancies[both]);
  const bool endgame = pieces <= 15;
  const U64 occ = td.occupancies[both];
  const U64 pw = td.bitboards[0], pb = td.bitboards[6];
  const U64 att_w = ((pw >> 9) & ~FILE_H) | ((pw >> 7) & ~FILE_A);   // il bianco avanza verso la riga 0
  const U64 att_b = ((pb << 7) & ~FILE_H) | ((pb << 9) & ~FILE_A);
  int score = 0;
  for (int c = white; c <= black; c++) {
    const int sgn = c == white ? 1 : -1;
    const U64 own = c == white ? pw : pb, opp = c == white ? pb : pw;
    // passati
    U64 passers = 0;
    for (U64 b = own; b; b &= b - 1) {
      const int sq = get_ls1b_index(b);
      if (!(T.ahead_adj[c][sq] & opp)) passers |= 1ULL << sq;
    }
    if (passers) {
      const int eksq = get_ls1b_index(td.bitboards[c == white ? 11 : 5]);
      for (U64 b = passers; b; b &= b - 1) {
        const int sq = get_ls1b_index(b), row = sq >> 3, f = sq & 7;
        const int rel = c == white ? 7 - row : row;   // 0 = prima traversa
        if (endgame) {
          score += sgn * ResPasserRank * rel;
          if (ResPasserFree) {
            const int prom = c == white ? f : 56 + f, dist = 7 - rel;
            const int kd = std::max(std::abs((eksq >> 3) - (prom >> 3)), std::abs((eksq & 7) - (prom & 7)));
            if (kd - (td.side == c ? 0 : 1) > dist) score += sgn * ResPasserFree;
          }
        }
        if (ResPasserConn && (passers & ((f > 0 ? T.file[f - 1] : 0) | (f < 7 ? T.file[f + 1] : 0))))
          score += sgn * ResPasserConn;
      }
    }
    // colonne senza nostri pedoni accanto al proprio re (mediogioco)
    if (!endgame && ResKingOpen) {
      const int kf = get_ls1b_index(td.bitboards[c == white ? 5 : 11]) & 7;
      for (int ff = std::max(0, kf - 1); ff <= std::min(7, kf + 1); ff++)
        if (!(own & T.file[ff])) score -= sgn * ResKingOpen;
    }
    // spazio: colonne c-f, nostre traverse 2-4, vuote e non attaccate da pedoni nemici
    if (ResSpace) {
      constexpr U64 CF = 0x3C3C3C3C3C3C3C3CULL;
      const U64 zone = CF & (c == white ? 0x00FFFFFF00000000ULL : 0x00000000FFFFFF00ULL);
      score += sgn * ResSpace * count_bits(zone & ~occ & ~(c == white ? att_b : att_w));
    }
  }
  return score;
}

// Valutazione nelle unita' dei margini della ricerca: x EvalUnits/1024, dentro la fascia delle tablebase.
static inline int static_evaluate(ThreadData &td) {
  int v = (int)((long long)net_evaluate(td) * EvalUnits / 1024);
  if (ResPasserRank | ResPasserFree | ResPasserConn | ResKingOpen | ResSpace) {
    // cp stampati -> unita' dei margini: inverso di units_to_cp (cp = v * 1024 * 25 / (EvalUnits * scala))
    const int cp = residual_terms_cp(td);
    v += (int)((long long)(td.side == white ? cp : -cp) * EvalUnits * nn_get_eval_scale() / (1024 * 25));
  }
  return std::clamp(v, -TB_WIN_FLOOR + 1, TB_WIN_FLOOR - 1);
}

// Valore per lo schermo: dalle unita' della ricerca ai centipedoni (scala della rete e normalizzazione 400).
// SCALA 06/10/2026 (richiesta dell'utente): 449 -> 400, cosi' +1,00 = 50% di probabilita' di vincere. Taratura sui
// NOSTRI dati di autogioco (match_moe/fit_cp_scale.py): con 449 il 50% di vittorie cadeva a +0,89 stampato, piatto
// col materiale (86-95 da 17 a 78 punti di materiale) sia a 10+0.1 (563.496 posizioni) sia a 40+0.4 (99.438):
// basta una costante, non serve una curva per materiale. Solo stampa: la ricerca usa le unita' interne, il bench non
// cambia. Cambiano le soglie di aggiudicazione delle GUI (stessa posizione = cifra +12%).
static inline int units_to_cp(int v) {
  const long long net = (long long)v * 1024 / EvalUnits;
  const int es = nn_get_eval_scale();
  const long long cp = es == 100 ? net : net * 100 / es;
  return (int)(cp * 100 / 400);
}

// ============================================================================
// STATISTICHE DELLE MOSSE
// ============================================================================

// Valore di un tipo di pezzo nelle unita' dei margini (indice: P..K, poi "nessuno").
static const int unit_value[7] = {208, 781, 825, 1276, 2538, 0, 0};

// Spinge una statistica verso il bonus: piu' la voce e' gia' vicina al limite, meno si muove.
static inline void stat_push(int16_t &e, int bonus, int limit) {
  const int b = std::clamp(bonus, -limit, limit);
  const int v = e;
  e = (int16_t)(v + b - v * (b < 0 ? -b : b) / limit);
}
static inline void stat_push(int &e, int bonus, int limit) {
  const int b = std::clamp(bonus, -limit, limit);
  e += b - e * (b < 0 ? -b : b) / limit;
}

constexpr int LIM_QUIET  = 7183;
constexpr int LIM_CAPT   = 10692;
constexpr int LIM_FOLLOW = 30000;
constexpr int LIM_PAWN   = 8192;
constexpr int LIM_CORR   = 1024;
constexpr int LIM_TTMOVE = 8192;

static inline bool move_ok(int m) { return m > 0; }
static inline int  move_from(int m) { return get_move_source(m); }
static inline int  move_to(int m) { return get_move_target(m); }
static inline int  move_piece(int m) { return get_move_piece(m); }
// Tipo di promozione 0..4 (nessuna, N, B, R, Q): entra nella chiave delle statistiche quiete.
// 04/10/2026 sera: tabella sui 4 bit del campo al posto di `pr ? pr % 6 : 0` (stessi valori, anche per i codici che
// non si presentano mai): promo_kind e' chiamata per ogni mossa da quiet_key e is_tactical e valeva lo 0,6% dei cicli.
static constexpr signed char kPromoKind[16] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3};
static inline int  promo_kind(int m) { return kPromoKind[get_move_promoted(m)]; }
static inline int  quiet_key(int m) { return move_from(m) | (move_to(m) << 6) | (promo_kind(m) << 12); }
// Mossa "tattica": cattura o promozione a donna (codice Q = 4 o q = 10: bit 4 e 10 della maschera 0x410). Le altre
// promozioni si ordinano fra le quiete.
static inline bool is_tactical(int m) { return (get_move_capture(m) | ((0x410u >> get_move_promoted(m)) & 1)) != 0; }
// Tipo di pezzo P..K da un codice P..k, senza modulo (06/10/2026, velocita', stessi valori): `pc % 6` costava una
// moltiplicazione e tre operazioni per mossa (score_quiets, checks_before, victim_kind...). L'indice 15 e' "nessun
// pezzo" (-1 & 15) e da' CAPT_NONE, cosi' anche la casa vuota passa dalla tabella senza salto.
static constexpr signed char kPieceKind[16] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, CAPT_NONE, CAPT_NONE, CAPT_NONE, CAPT_NONE};
// Tipo del pezzo catturato (CAPT_NONE per l'en passant e per la promozione senza cattura, come la casa vuota).
static inline int  victim_kind(const ThreadData &td, int m) {
  return kPieceKind[td.piece_on[move_to(m)] & 15];
}

static inline PieceToRow &pawn_row(const ThreadData &td) {
  return g_stats.pawn_rows[td.pawn_key & g_stats.pawn_mask];
}

// Statistiche di seguito: la mossa (pc, to) dopo le mosse fatte 1..6 ply prima. Pesi per distanza e un fattore
// che cresce con il numero di distanze che gia' la premiano.
static void learn_followups(NodeFrame *f, int pc, int to, int bonus) {
  static const int dist_weight[7] = {0, 520, 390, 145, 251, 66, 209};
  static const int agree_mul[7] = {94, 103, 110, 106, 119, 126, 121};
  int agreeing = 0;
  for (int d = 1; d <= 6; d++) {
    if (f->in_check && d > 2)
      break;
    if (!move_ok(f[-d].move))
      continue;
    int16_t &e = (*f[-d].cont_hist)[pc][to];
    if (e > 0)
      agreeing++;
    stat_push(e, bonus * dist_weight[d] * agree_mul[agreeing] / 65536 + (d < 2 ? 73 : 0), LIM_FOLLOW);
  }
}

static void learn_quiet(ThreadData &td, NodeFrame *f, int m, int bonus) {
  const int key = quiet_key(m);
  stat_push(td.quiet_hist[td.side][key], bonus, LIM_QUIET);
  if (f->ply < LOW_PLY_SIZE)
    stat_push(td.low_ply_hist[f->ply][key], bonus * LowPlyScale / 1024, LIM_QUIET);
  learn_followups(f, move_piece(m), move_to(m), bonus * ContUpdScale / 1024);
  stat_push(pawn_row(td)[move_piece(m)][move_to(m)],
            bonus * (bonus > -4 ? PawnBonusScale : PawnMalusScale) / 1024, LIM_PAWN);
}

static inline void learn_capture(ThreadData &td, int m, int bonus) {
  stat_push(td.capt_hist[move_piece(m)][move_to(m)][victim_kind(td, m)], bonus, LIM_CAPT);
}

// ============================================================================
// CORREZIONE DELLA VALUTAZIONE STATICA
// Si impara la differenza fra il valore cercato e la valutazione statica, per chiavi di struttura (pedoni, pezzi
// minori, non-pedoni di ciascun colore) e per la coppia di mosse che porta al nodo.
// ============================================================================

static inline CorrSlot &corr_slot(U64 key, int side_to_move) {
  return g_stats.corr[key & g_stats.corr_mask][side_to_move];
}

// Idea 9 (CorrPhase, 04/10/2026): con quattro esperti l'errore sistematico su una stessa struttura cambia con
// l'esperto. Una costante per fascia mescolata nelle chiavi dei pedoni e dei minori separa le correzioni.
// 06/10/2026 (velocita', stessi valori): la chiave si legge da una tabella per numero di pezzi, senza i due o
// tre confronti di net_phase. Contatori: ~2,1 chiamate per nodo (prefetch nella make, corr_signal, corr_learn).
static constexpr auto corr_phase_keys_by_count = [] {
  constexpr U64 keys[4] = {0x9E3779B97F4A7C15ULL, 0xC2B2AE3D27D4EB4FULL, 0x165667B19E3779F9ULL, 0xD6E8FEB86659FD93ULL};
  std::array<U64, 65> t{};   // fino a 64: anche una FEN con troppi pezzi resta nella tabella
  for (int c = 0; c <= 64; c++)
    t[c] = keys[c <= 9 ? 0 : c <= 15 ? 1 : c <= 23 ? 2 : 3];   // = net_phase(c)
  return t;
}();
static inline U64 corr_phase_key(const ThreadData &td) {
  return CorrPhase ? corr_phase_keys_by_count[count_bits(td.occupancies[both])] : 0;
}

// CorrFifty (nostra, 05/10/2026, da un'idea di Reckless): l'errore sistematico della valutazione su una stessa
// struttura cambia con la distanza dalla regola delle 50 mosse (la rete smorza l'eval col contatore, e nei finali
// che si trascinano la posizione "vale" altro). Una costante per fascia di CorrFiftyStep semimosse (al massimo 16
// fasce) mescolata nelle chiavi dei pedoni e dei non-pedoni separa le correzioni. A 0 la chiave e' quella di sempre.
static inline U64 corr_fifty_key(const ThreadData &td) {
  static constexpr U64 keys[16] = {0, 0x78FAC4B00841A538ULL, 0x5EB2C61E9C3747D9ULL, 0x59701BD515B6EE59ULL,
                                   0x2FEFFD3F283182A5ULL, 0x2DBE4F221F31A14FULL, 0xE19215DA308D5DA7ULL,
                                   0x4AA99EFB91350BC7ULL, 0xC9C3DD626F1AE010ULL, 0x642CD00D7AA43578ULL,
                                   0x4E3591F9E5FC0494ULL, 0x3FBCACD31257FB99ULL, 0x0C995B8887B93F26ULL,
                                   0x8408B9E4AF9644A9ULL, 0x10A6D573C576C901ULL, 0x5DDBAD22D85BAB7BULL};
  return CorrFiftyStep ? keys[std::min(td.fifty / CorrFiftyStep, 15)] : 0;
}

static int corr_signal(const ThreadData &td, const NodeFrame *f) {
  PROF_GUARD(prof_corr);
  const int us = td.side;
  const U64 phk = corr_phase_key(td);
  const U64 fk = corr_fifty_key(td);
  const int pawn = corr_slot(td.pawn_key ^ phk ^ fk, us).pawn;
  const int minor = corr_slot(td.mm_key[0] ^ phk, us).minor;
  const int npw = corr_slot(td.np_key[white] ^ fk, us).np_white;
  const int npb = corr_slot(td.np_key[black] ^ fk, us).np_black;
  const int prev = f[-1].move;
  int cont = CorrNoMoveW;   // termine fisso dopo la mossa nulla (08/10/2026: era la costante 64049, ora leva)
  if (move_ok(prev)) {
    const int to = move_to(prev), pc = td.piece_on[to];
    cont = CorrContW * ((*f[-2].cont_corr)[pc][to] + (*f[-4].cont_corr)[pc][to]);
    if (CorrCont6W)   // f[-6] esiste sempre: prima della radice ci sono 7 frame con la riga sentinella
      cont += CorrCont6W * (*f[-6].cont_corr)[pc][to];
    // CorrContOppW (idea dell'utente, 06/10/2026): la coppia delle due ultime mosse dell'AVVERSARIO (a -3 e a -1),
    // cioe' il suo piano, invece della nostra mossa con la sua risposta. Stessa tabella; la versione con la sola
    // ultima mossa, senza contesto, era chiusa (-7,6 +- 7,8 a 20+0.2).
    if (CorrContOppW)
      cont += CorrContOppW * (*f[-3].cont_corr)[pc][to];
  }
  return CorrPawnW * pawn + CorrMinorW * minor + CorrNonPawnW * (npw + npb) + cont;
}

// CorrExpertW (06/10/2026, rapporto N_CORREZIONI 4.8): una tabella dei pedoni per esperto della rete che si somma
// alle correzioni condivise invece di sostituirne la chiave (come CorrPhase). La condivisa resta come base, quella per
// esperto impara solo la differenza propria dell'esperto. Il termine entra nella valutazione ma NON in n.signal, che
// regola RFP, LMR e singolare (rapporto 4.2): cosi' si misura solo la precisione. A 0 il termine vale 0.
static inline U64 corr_expert_key(const ThreadData &td) {
  static constexpr U64 keys[4] = {0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL, 0x510E527FADE682D1ULL,
                                  0x9B05688C2B3E6C1FULL};
  const int pcs = count_bits(td.occupancies[both]);
  const int band = CorrExpertBands == 2 ? (pcs <= 15 ? 0 : 3) : net_phase(pcs);
  return td.pawn_key ^ keys[band];
}

static inline int corr_signal_extra(const ThreadData &td) {
  if (!CorrExpertW)
    return 0;
  return CorrExpertW * g_stats.corr_x[corr_expert_key(td) & g_stats.corr_mask][td.side];
}

static inline int corrected_eval(int v, int signal) {
  return std::clamp(v + signal / 131072, -TB_WIN_FLOOR + 1, TB_WIN_FLOOR - 1);
}

static void corr_learn(ThreadData &td, NodeFrame *f, int bonus) {
  PROF_GUARD(prof_corr);
  const int us = td.side;
  const U64 phk = corr_phase_key(td);
  const U64 fk = corr_fifty_key(td);
  stat_push(corr_slot(td.pawn_key ^ phk ^ fk, us).pawn, bonus * CorrPawnUpd / 128, LIM_CORR);
  stat_push(corr_slot(td.mm_key[0] ^ phk, us).minor, bonus * CorrMinorUpd / 128, LIM_CORR);
  stat_push(corr_slot(td.np_key[white] ^ fk, us).np_white, bonus * CorrNonPawnUpd / 128, LIM_CORR);
  stat_push(corr_slot(td.np_key[black] ^ fk, us).np_black, bonus * CorrNonPawnUpd / 128, LIM_CORR);
  const int prev = f[-1].move;
  if (move_ok(prev)) {
    const int to = move_to(prev), pc = td.piece_on[to];
    stat_push((*f[-2].cont_corr)[pc][to], bonus * CorrCont2Upd / 128, LIM_CORR);
    stat_push((*f[-4].cont_corr)[pc][to], bonus * CorrCont4Upd / 128, LIM_CORR);
    if (CorrCont6W)
      stat_push((*f[-6].cont_corr)[pc][to], bonus * CorrCont6Upd / 128, LIM_CORR);
    if (CorrContOppW)
      stat_push((*f[-3].cont_corr)[pc][to], bonus * CorrContOppUpd / 128, LIM_CORR);
  }
  if (CorrExpertW)
    stat_push(g_stats.corr_x[corr_expert_key(td) & g_stats.corr_mask][us], bonus * CorrExpertUpd / 128, LIM_CORR);
}
