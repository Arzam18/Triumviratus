// search/11_queue.inc -- Ordinamento delle mosse.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

// SEE con la convenzione della ricerca: promozioni, arrocco ed en passant valgono 0.
static inline bool see_holds(ThreadData &td, int m, int threshold) {
  if (get_move_promoted(m) || get_move_castling(m) || get_move_enpassant(m))
    return 0 >= threshold;
  PROF_GUARD(prof_see);
  return td_see_ge(td, m, threshold) != 0;
}

// ============================================================================
// Una coda per nodo. Le mosse escono a gruppi: la mossa hash, le tattiche che reggono lo scambio, le quiete con
// buone statistiche, le tattiche perdenti, le quiete rimaste. Sotto scacco si ordinano tutte le evasioni insieme;
// in quiescenza e per i tagli (probcut) si vedono solo le tattiche.
// Le mosse sono pseudo-legali: la legalita' la decide td_make_move.
// ============================================================================
enum QueueKind { QK_MAIN, QK_EVADE, QK_QUIESCE, QK_CUTS };

enum QueueStep {
  QS_HASH,
  QS_TACT_FILL, QS_TACT_WIN, QS_QUIET_FILL, QS_QUIET_HIGH, QS_TACT_LOSE, QS_QUIET_LOW,
  QS_EVADE_FILL,
  QS_ONLY_TACT_FILL,
  QS_DRAIN,
  QS_DONE
};

struct ScoredMove {
  int move;
  int score;
};

struct MoveQueue {
  ThreadData   *td;
  NodeFrame    *f;
  int           kind;
  int           step;
  int           hash_move;
  int           depth;
  int           cut_threshold;   // QK_CUTS: soglia di SEE
  bool          quiets_off;      // potatura per numero di mosse: niente piu' quiete
  LmpChkCtx    *checks;          // case di scacco del nodo, condivise con la legalita' (null = calcolate qui)
  int           n_tact, n_all;   // tattiche in [0, n_tact), quiete in [n_tact, n_all)
  int           cur;
  int           n_lose, cur_lose;
  ScoredMove    list[256];
  unsigned char losers[256];
};

// Inserimento stabile: le mosse con punteggio >= limit vanno in testa in ordine decrescente, le altre restano
// dopo, nell'ordine di generazione.
// 04/10/2026 (xperf): qui nasce l'11% dei salti mal predetti del motore (~6 per nodo; l'ordinamento intero ne fa 10,
// SF 2,7). PROVATA e TOLTA la variante a scambio (la mossa buona prende il posto della prima bassa): salti mal
// predetti invariati (55,4 -> 55,1 per nodo), cicli per nodo +3,8%, ordine delle mosse basse rimescolato. I salti
// vengono dai confronti, che dipendono dai dati, non dagli spostamenti: serve un ordinamento senza salti.
// 04/10/2026 — SENZA SALTI, stesso ordine di prima (albero identico). Ogni mossa sopra la soglia ha una chiave unica
// (punteggio << 8 | 255 - posizione): a parita' di punteggio vince la prima generata, come nell'inserimento stabile.
// La sua posizione finale e' il numero di chiavi piu' grandi della sua, contato con un ciclo che il compilatore
// vettorizza; le mosse sotto la soglia seguono nell'ordine di generazione. Le divisioni fra le due liste si fanno con
// incrementi condizionati, non con salti. n <= 256, quindi la posizione sta in 8 bit.
// Misura (xperf, PGO, nodi identici): con chiavi a 64 bit salti mal predetti -10,8% per nodo ma istruzioni +1,2% e
// cicli invariati. Chiavi a 32 bit (Wide = false): 16 confronti per istruzione AVX-512 invece di 8. Valgono per
// tattiche e quiete, i cui punteggi stanno ben dentro +-2^22 (la saturazione e' solo una sicurezza); le parate dallo
// scacco usano 1 << 28 per le catture e restano a 64 bit.
template <bool Wide>
static void rank_from(ScoredMove *a, int n, int limit) {
  PROF_GUARD(prof_mp);
  using Key = std::conditional_t<Wide, long long, int>;
  ScoredMove tmp[256];
  alignas(64) Key key[256];
  unsigned char hi[256], lo[256];
  int nh = 0, nl = 0;
  for (int i = 0; i < n; i++) {
    tmp[i] = a[i];
    const int up = a[i].score >= limit;
    hi[nh] = (unsigned char)i;
    lo[nl] = (unsigned char)i;
    nh += up;
    nl += 1 - up;
  }
  for (int k = 0; k < nh; k++) {
    const int s = tmp[hi[k]].score;
    if constexpr (Wide)
      key[k] = ((long long)s << 8) | (255 - hi[k]);
    else
      key[k] = (int)((unsigned)std::clamp(s, -(1 << 22), (1 << 22) - 1) << 8) | (255 - hi[k]);
  }
  for (int k = 0; k < nh; k++) {
    const Key x = key[k];
    int r = 0;
    for (int j = 0; j < nh; j++)
      r += key[j] > x;
    a[r] = tmp[hi[k]];
  }
  for (int k = 0; k < nl; k++)
    a[nh + k] = tmp[lo[k]];
}

static void queue_start(MoveQueue &q, ThreadData &td, NodeFrame *f, int hash_move, int depth, int kind) {
  q.td = &td;
  q.f = f;
  q.kind = kind;
  q.depth = depth;
  q.quiets_off = false;
  q.checks = nullptr;
  q.cut_threshold = 0;
  q.n_tact = q.n_all = q.cur = 0;
  q.n_lose = q.cur_lose = 0;
  bool hash_ok = hash_move > 0 && td_is_pseudo_legal(td, hash_move);
  if (kind == QK_CUTS)
    hash_ok = hash_ok && is_tactical(hash_move);
  q.hash_move = hash_ok ? hash_move : 0;
  q.step = QS_HASH;
}

static inline int score_tactic(const ThreadData &td, int m) {
  const int vk = victim_kind(td, m);
  return td.capt_hist[move_piece(m)][move_to(m)][vk] + 7 * unit_value[vk];
}

// Punteggi delle quiete: statistiche (pesate), bonus agli scacchi diretti che non perdono materiale, e la mossa
// di un pezzo che esce da (o entra in) una casa attaccata da un pezzo nemico di valore minore.
static void score_quiets(MoveQueue &q, int first, int last) {
  PROF_GUARD(prof_score);
  ThreadData &td = *q.td;
  NodeFrame *f = q.f;
  const int us = td.side, them = us ^ 1;
  const int eoff = them == white ? 0 : 6;
  const U64 occ = td.occupancies[both];

  U64 by_minor = 0, by_rook = 0;
  // Case attaccate da tutti i pedoni avversari insieme: gli stessi due shift di mask_pawn_attacks (attacks.cpp)
  // applicati alla bitboard intera, quindi la stessa unione del ciclo per pedone (04/10/2026).
  const U64 epawns = td.bitboards[P + eoff];
  const U64 by_pawn = them == white ? ((epawns >> 7) & not_a_file) | ((epawns >> 9) & not_h_file)
                                    : ((epawns << 7) & not_h_file) | ((epawns << 9) & not_a_file);
  U64 bb = td.bitboards[N + eoff];
  while (bb) { by_minor |= knight_attacks[get_ls1b_index(bb)]; pop_lsb_bb(bb); }
  bb = td.bitboards[B + eoff];
  while (bb) { by_minor |= get_bishop_attacks(get_ls1b_index(bb), occ); pop_lsb_bb(bb); }
  bb = td.bitboards[R + eoff];
  while (bb) { by_rook |= get_rook_attacks(get_ls1b_index(bb), occ); pop_lsb_bb(bb); }
  U64 lesser[6];
  lesser[P] = 0;
  lesser[N] = lesser[B] = by_pawn;
  lesser[R] = by_minor | by_pawn;
  lesser[Q] = by_rook | lesser[R];
  lesser[K] = 0;

  LmpChkCtx local_checks;
  LmpChkCtx &checks = q.checks ? *q.checks : local_checks;
  if (!checks.ready)
    td_lmp_chk_init(td, checks);

  // Scudo del re (nostra): col re sulla traversa di casa, i nostri pedoni accanto al re si muovono per ultimi.
  // Vale fino al mediogioco: solo finche' l'avversario ha materiale d'attacco (KingShieldNpm, pezzi non pedoni).
  U64 shield = 0;
  if (KingShield) {
    const int ksq = get_ls1b_index(td.bitboards[us == white ? K : k]);
    const bool home = us == white ? ksq >= a1 : ksq <= h8;
    const int their_npm = unit_value[N] * count_bits(td.bitboards[N + eoff]) +
                          unit_value[B] * count_bits(td.bitboards[B + eoff]) +
                          unit_value[R] * count_bits(td.bitboards[R + eoff]) +
                          unit_value[Q] * count_bits(td.bitboards[Q + eoff]);
    if (home && their_npm >= KingShieldNpm)
      shield = king_attacks[ksq] & td.bitboards[us == white ? P : p];
  }

  const PieceToRow &c1 = *f[-1].cont_hist, &c2 = *f[-2].cont_hist, &c3 = *f[-3].cont_hist,
                   &c4 = *f[-4].cont_hist, &c6 = *f[-6].cont_hist;
  const PieceToRow &prow = pawn_row(td);
  const int ply = f->ply;

  for (int i = first; i < last; i++) {
    const int m = q.list[i].move;
    const int pc = move_piece(m), to = move_to(m), from = move_from(m), pt = pc % 6;
    const int key = quiet_key(m);
    int s = 2 * td.quiet_hist[us][key] + 2 * prow[pc][to];
    s += c1[pc][to] + c2[pc][to] + c3[pc][to] + c4[pc][to] + c6[pc][to];
    if (((checks.chk[pt] >> to) & 1) && see_holds(td, m, -75))
      s += CheckOrderBonus;
    s += unit_value[pt] * ThreatOrderMul * (int((lesser[pt] >> from) & 1) - int((lesser[pt] >> to) & 1));
    if (ply < LOW_PLY_SIZE)
      s += 8 * td.low_ply_hist[ply][key] / (1 + ply);
    if (shield >> from & 1)
      s -= KingShield;
    q.list[i].score = s;
  }
}

static void fill_tactics(MoveQueue &q) {
  ThreadData &td = *q.td;
  moves ml[1];
  td_generate_moves(td, ml, /*captures_only=*/true, /*quiets_only=*/false, /*known_in_check=*/0,
                    /*promo_quiet=*/true, /*promo_queen_only=*/true);
  for (int i = 0; i < ml->count; i++) {
    q.list[i].move = ml->moves[i];
    q.list[i].score = score_tactic(td, ml->moves[i]);
  }
  q.n_tact = q.n_all = ml->count;
  rank_from<false>(q.list, q.n_tact, INT_MIN);
  q.cur = 0;
}

static void fill_quiets(MoveQueue &q) {
  ThreadData &td = *q.td;
  moves ml[1];
  td_generate_moves(td, ml, /*captures_only=*/false, /*quiets_only=*/true, /*known_in_check=*/0);
  int n = q.n_tact;
  for (int i = 0; i < ml->count; i++)
    if (promo_kind(ml->moves[i]) != Q)   // la promozione a donna e' gia' fra le tattiche
      q.list[n++].move = ml->moves[i];
  q.n_all = n;
  score_quiets(q, q.n_tact, n);
  rank_from<false>(q.list + q.n_tact, n - q.n_tact, -QuietSortDepthMul * q.depth);
  q.cur = q.n_tact;
}

static void fill_evasions(MoveQueue &q) {
  ThreadData &td = *q.td;
  moves ml[1];
  td_generate_moves(td, ml, false, false, /*known_in_check=*/1);
  const PieceToRow &c1 = *q.f[-1].cont_hist;
  for (int i = 0; i < ml->count; i++) {
    const int m = ml->moves[i];
    q.list[i].move = m;
    q.list[i].score = is_tactical(m) ? unit_value[victim_kind(td, m)] + (1 << 28)
                                     : td.quiet_hist[td.side][quiet_key(m)] + c1[move_piece(m)][move_to(m)];
  }
  q.n_all = q.n_tact = ml->count;
  rank_from<true>(q.list, q.n_all, INT_MIN);
  q.cur = 0;
}

// Prossima mossa pseudo-legale, 0 a coda finita. La mossa hash esce una volta sola.
static int queue_next(MoveQueue &q) {
  ThreadData &td = *q.td;
  for (;;) {
    switch (q.step) {
    case QS_HASH:
      q.step = q.kind == QK_EVADE ? QS_EVADE_FILL : q.kind == QK_MAIN ? QS_TACT_FILL : QS_ONLY_TACT_FILL;
      if (q.hash_move)
        return q.hash_move;
      break;

    case QS_TACT_FILL:
      fill_tactics(q);
      q.step = QS_TACT_WIN;
      break;

    case QS_TACT_WIN:
      while (q.cur < q.n_tact) {
        const ScoredMove sm = q.list[q.cur++];
        if (sm.move == q.hash_move)
          continue;
        if (see_holds(td, sm.move, -sm.score / GoodCaptSeeDiv))
          return sm.move;
        q.losers[q.n_lose++] = (unsigned char)(q.cur - 1);
      }
      q.step = QS_QUIET_FILL;
      break;

    case QS_QUIET_FILL:
      if (!q.quiets_off)
        fill_quiets(q);
      q.step = QS_QUIET_HIGH;
      break;

    case QS_QUIET_HIGH:
      while (!q.quiets_off && q.cur < q.n_all) {
        const ScoredMove sm = q.list[q.cur++];
        if (sm.move != q.hash_move && sm.score > GoodQuietLimit)
          return sm.move;
      }
      q.step = QS_TACT_LOSE;
      break;

    case QS_TACT_LOSE:
      if (q.cur_lose < q.n_lose)
        return q.list[q.losers[q.cur_lose++]].move;
      q.cur = q.n_tact;
      q.step = QS_QUIET_LOW;
      break;

    case QS_QUIET_LOW:
      while (!q.quiets_off && q.cur < q.n_all) {
        const ScoredMove sm = q.list[q.cur++];
        if (sm.move != q.hash_move && sm.score <= GoodQuietLimit)
          return sm.move;
      }
      q.step = QS_DONE;
      break;

    case QS_EVADE_FILL:
      fill_evasions(q);
      q.step = QS_DRAIN;
      break;

    case QS_ONLY_TACT_FILL:
      fill_tactics(q);
      q.step = QS_DRAIN;
      break;

    case QS_DRAIN:
      while (q.cur < q.n_tact) {
        const ScoredMove sm = q.list[q.cur++];
        if (sm.move == q.hash_move)
          continue;
        if (q.kind == QK_CUTS && !see_holds(td, sm.move, q.cut_threshold))
          continue;
        return sm.move;
      }
      q.step = QS_DONE;
      break;

    default:
      return 0;
    }
  }
}
