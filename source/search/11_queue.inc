// search/11_queue.inc -- Ordinamento delle mosse.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

// SeePinned (05/10/2026, audit del nucleo): inchiodati e inchiodatori dei due colori, una volta per posizione, per la
// SEE (see.cpp). Per ogni re: i pezzi lunghi avversari sulle sue linee a scacchiera vuota sono i cecchini; un
// cecchino con un solo pezzo in mezzo, e quel pezzo nostro, lo inchioda. SF fa lo stesso conto in ogni posizione.
static void see_pins_update(ThreadData &td) {
  for (int c = white; c <= black; c++) {
    const int off = c == white ? 0 : 6, eoff = 6 - off;
    const int ksq = get_ls1b_index(td.bitboards[K + off]);
    U64 snipers = (empty_rook_att[ksq] & (td.bitboards[R + eoff] | td.bitboards[Q + eoff])) |
                  (empty_bishop_att[ksq] & (td.bitboards[B + eoff] | td.bitboards[Q + eoff]));
    const U64 occ = td.occupancies[both] ^ snipers;
    U64 pinned = 0, pinners = 0;
    while (snipers) {
      const int sn = get_ls1b_index(snipers);
      const U64 b = between_tbl[ksq][sn] & occ;
      if (b && !(b & (b - 1)) && (b & td.occupancies[c])) {
        pinned |= b;
        pinners |= 1ULL << sn;
      }
      pop_lsb_bb(snipers);
    }
    td.see_pinned[c] = pinned;
    td.see_pinners[c ^ 1] = pinners;
  }
  td.see_pin_key = td.hash_key;
}

// SEE with the convention of the search: castling and en passant count 0. Promotions go to the SEE, which adds the
// promotion gain (17/10/2026, review by Bastiii; before, they counted 0 too).
static inline bool see_holds(ThreadData &td, int m, int threshold) {
  if (get_move_castling(m) || get_move_enpassant(m))
    return 0 >= threshold;
  PROF_GUARD(prof_see);
  if (SeePinned && td.see_pin_key != td.hash_key)
    see_pins_update(td);
  return td_see_ge(td, m, threshold) != 0;
}

// ============================================================================
// THE MOVE QUEUE (block 1 of 8.1, sub-block D)
// One queue per node hands out the moves in groups: hash move, captures that hold the exchange, quiet moves with good
// statistics, losing captures, the remaining quiet moves. In check all evasions are sorted together; in quiescence
// and ProbCut only captures are seen. Moves are pseudo-legal: td_make_move decides legality.
// Origin: staged move generation is a known idea, common to almost every engine; the order of the groups follows the
// algorithmic base of Stockfish 19. Ours: the three thresholds between the groups are per expert (StGood, StQuiet,
// StSort), chosen once per node when the queue fills (q.ex, q.good_capt, q.good_quiet). Defaults give the old
// constants, identical tree. Code map: docs/conceptual_maps/BLOCK_1_CODE_MAP.md.
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
  // Attacchi dell'avversario nella posizione del nodo, calcolati da score_quiets (06/10/2026, SEE delle quiete):
  // opp_att = tutte le case che attacca, opp_slider = quelle viste dai suoi pezzi lunghi. Valide se opp_ready.
  bool          opp_ready;
  U64           opp_att, opp_slider;
  // ThreatHist: threatened squares per type of our piece (09_history.inc), written by score_quiets from the same maps
  // as the ordering; valid when menace_ready.
  bool          menace_ready;
  U64           menace[6];
  // V2 (08/10/2026): confine fra quiete buone e cattive gia' noto dopo l'ordinamento (quiet_split >= 0), oppure -1
  // (si scorre la lista confrontando ogni punteggio, come prima).
  int           quiet_split;
  LegalNode     lgen;            // GL: inchiodature, scacchi e maschere del nodo, condivisi dalle fasi
  // LmrRank (blocco 2 della 8.1, 16/10/2026): rank_ok = l'ultima mossa uscita e' una quieta della lista ordinata, con
  // punteggio d'ordinamento last_score; quiet_top = il punteggio piu' alto fra le quiete del nodo (fill_quiets).
  bool          rank_ok;
  int           last_score, quiet_top;
  // Expert of the node and group thresholds: written by fill_tactics / fill_evasions (ex, good_capt) and fill_quiets
  // (good_quiet), read by queue_next.
  int           ex, good_capt, good_quiet;
};

// ThreatHist: threat context of a quiet move, from the node's maps when they exist, otherwise from its two squares.
static inline int quiet_ctx(const MoveQueue &q, int m) {
  if (!ThreatHist || get_move_promoted(m))
    return 0;
  if (q.menace_ready) {
    const U64 mm = q.menace[kPieceKind[move_piece(m)]];
    return (int)((mm >> move_from(m)) & 1) | (int)((mm >> move_to(m)) & 1) << 1;
  }
  return quiet_ctx_slow(*q.td, m);
}

// SEE di una mossa quieta decisa senza scambio (06/10/2026, velocita', albero identico). Per una quieta semplice
// (casa d'arrivo vuota, niente promozione, arrocco, en passant) td_see_ge esce con "soglia > 0 -> no" o, per
// soglia <= 0, con "si'" appena l'avversario non ha attaccanti sulla casa d'arrivo. Gli attaccanti li conta con
// l'occupazione SENZA la casa di partenza: togliere quella casa apre linee solo ai pezzi lunghi che gia' la vedono.
// Quindi, se la casa d'arrivo non e' attaccata dall'avversario e nessun suo pezzo lungo vede la casa di partenza,
// il risultato e' gia' noto: 0 >= soglia. La mappa la prepara score_quiets per le quiete della coda; la mossa hash
// (che esce prima della mappa) e le parate dallo scacco passano dalla SEE di sempre.
// Contatori (bench 14 su fens30): ~0,57 SEE di quiete per nodo.
static inline bool see_holds_q(ThreadData &td, const MoveQueue &q, int m, int threshold) {
  constexpr int SPECIAL = 0x100000 | 0xf0000 | 0x400000 | 0x800000;   // cattura, promozione, e.p., arrocco
  if (q.opp_ready && !(m & SPECIAL) && !((q.opp_att >> move_to(m)) & 1) && !((q.opp_slider >> move_from(m)) & 1)) {
#ifdef TRIUMV_VERIFY_SEEQ   // verifica: la SEE vera deve dare lo stesso esito
    if ((0 >= threshold) != see_holds(td, m, threshold)) {
      printf("info string SEEQ SBAGLIATA: mossa %d soglia %d\n", m, threshold);
      fflush(stdout);
      abort();
    }
#endif
    return 0 >= threshold;
  }
  return see_holds(td, m, threshold);
}

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
// tattiche e quiete, i cui punteggi stanno ben dentro +-2^22 (la saturazione e' solo una sicurezza). Dal 05/10/2026
// anche le parate dallo scacco (fill_evasions): prima usavano 1 << 28 per le catture e chiavi a 64 bit.
// 04/10/2026 notte — SENZA COPIE: le mosse arrivano dalla lista del generatore (mv) con i punteggi accanto (sc) e
// vengono scritte una volta sola, gia' in ordine, nella lista della coda (out). Prima passavano da tre liste
// (generatore -> coda -> copia temporanea dell'ordinamento). Stesso ordine, stesso albero.
#if defined(__AVX512F__) || defined(__AVX2__)
#include <immintrin.h>
#endif
// 05/10/2026 — CONTEGGIO A MASCHERE (AVX-512). Il ciclo `r += key[j] > x` ha un conteggio nh che cambia a ogni
// chiamata: il compilatore lo vettorizza ma lascia una coda scalare di nh % 16 giri (ognuno confronto, somma e salto)
// e una riduzione orizzontale per ogni mossa. Qui le chiavi oltre nh valgono INT_MIN (sotto ogni chiave vera, che e'
// almeno -(1 << 30)), cosi' un gruppo parziale si confronta intero: per ogni mossa 16 confronti alla volta ->
// maschera -> popcount, niente coda. La chiave si scrive gia' nel primo passaggio, nello slot corrente della lista
// alta, e conta solo se la mossa ci entra: il secondo ciclo su hi[] e' sparito. Stesse chiavi, stesse posizioni.
// xperf 6 giri, build PGO avx512: cicli per nodo -0,70%. Le build avx2 tengono il ciclo scalare.
// Restituisce quante mosse sono finite nella parte ordinata (punteggio >= limit).
template <bool Wide>
static int rank_into(ScoredMove *out, const int *mv, const int *sc, int n, int limit) {
  PROF_GUARD(prof_mp);
  using Key = std::conditional_t<Wide, long long, int>;
  alignas(64) Key key[256 + 16];   // 16 slot in piu' per il riempimento INT_MIN del percorso vettoriale
  unsigned char hi[256], lo[256];
  int nh = 0, nl = 0;
  for (int i = 0; i < n; i++) {
    const int s  = sc[i];
    const int up = s >= limit;
    hi[nh] = (unsigned char)i;
    lo[nl] = (unsigned char)i;
    if constexpr (Wide)
      key[nh] = ((long long)s << 8) | (255 - i);
    else
      key[nh] = (int)((unsigned)std::clamp(s, -(1 << 22), (1 << 22) - 1) << 8) | (255 - i);
    nh += up;
    nl += 1 - up;
  }
#if defined(__AVX512F__)
  if constexpr (!Wide) {
    _mm512_storeu_si512(key + nh, _mm512_set1_epi32(INT_MIN));   // riempimento fino al gruppo di 16 successivo
    const int nchunk = (nh + 15) >> 4;
    for (int k = 0; k < nh; k++) {
      const __m512i x = _mm512_set1_epi32(key[k]);
      int r = 0;
      for (int c = 0; c < nchunk; c++)
        r += count_bits((U64)_mm512_cmpgt_epi32_mask(_mm512_load_si512(key + 16 * c), x));
      out[r] = {mv[hi[k]], sc[hi[k]]};
    }
  } else
#elif defined(__AVX2__)
  // R1 (15/10/2026): lo stesso conteggio a gruppi pieni sulle build AVX2, 8 chiavi per confronto (vpcmpgtd ->
  // movmskps -> popcnt). Il ciclo scalare aveva un numero di giri nh diverso a ogni chiamata e la sua coda (nh % 8
  // giri dopo la parte vettoriale) usciva con un salto mal predetto quasi a ogni mossa ordinata (profilo dei salti mal
  // predetti, bench 20: ~2,6% del totale nelle righe di rank_into). Qui ogni mossa fa lo stesso numero di gruppi.
  // Stesse chiavi, stesse posizioni.
  if constexpr (!Wide) {
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(key + nh), _mm256_set1_epi32(INT_MIN));
    const int nchunk = (nh + 7) >> 3;
    for (int k = 0; k < nh; k++) {
      const __m256i x = _mm256_set1_epi32(key[k]);
      int r = 0;
      for (int c = 0; c < nchunk; c++)
        r += count_bits((U64)(unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(
                 _mm256_cmpgt_epi32(_mm256_load_si256(reinterpret_cast<const __m256i *>(key + 8 * c)), x))));
      out[r] = {mv[hi[k]], sc[hi[k]]};
    }
  } else
#endif
  for (int k = 0; k < nh; k++) {
    const Key x = key[k];
    int r = 0;
    for (int j = 0; j < nh; j++)
      r += key[j] > x;
    out[r] = {mv[hi[k]], sc[hi[k]]};
  }
  for (int k = 0; k < nl; k++)
    out[nh + k] = {mv[lo[k]], sc[lo[k]]};
  return nh;
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
  q.opp_ready = false;
  q.menace_ready = false;
  q.quiet_split = -1;
  q.lgen.ready = false;
  q.rank_ok = false;
  q.last_score = q.quiet_top = 0;
  q.ex = 3, q.good_capt = 1024, q.good_quiet = 0;
  bool hash_ok = hash_move > 0 && td_is_pseudo_legal(td, hash_move);
  if (kind == QK_CUTS)
    hash_ok = hash_ok && is_tactical(hash_move);
  q.hash_move = hash_ok ? hash_move : 0;
  q.step = QS_HASH;
}

// Score of a capture (sub-block C): capture history and value of the victim, weighted per expert (HtCapt, HtVict,
// /1024; the defaults 1024 and 7168 give the old sum history + 7 x value). The score also sets the exchange threshold
// of a good capture in queue_next (-score x StGood / 19456).
// PromoOrder and BishopPairCapt (08/10/2026, off = tree unchanged): queen promotions and captures of a bishop of the
// enemy pair move up in the order.
static inline int score_tactic(const ThreadData &td, int m, int ex) {
  const int vk = victim_kind(td, m);
  int s = (by_exp4(ex, HtCapt0, HtCapt1, HtCapt2, HtCapt3) * td.capt_hist[move_piece(m)][move_to(m)][vk] +
           by_exp4(ex, HtVict0, HtVict1, HtVict2, HtVict3) * unit_value[vk]) / 1024;
  if (PromoOrder && promo_kind(m) == Q)
    s += PromoOrder;
  if (BishopPairCapt && vk == B && count_bits(td.bitboards[td.side == white ? b : B]) >= 2)
    s += BishopPairCapt;
  return s;
}

// AA2 (10/10/2026): case attaccate dall'insieme dei cavalli in `b`, con gli spostamenti di mask_knight_attacks
// (attacks.cpp) applicati alla bitboard intera: la stessa unione del ciclo su knight_attacks[]. Numerazione a8 = 0:
// la colonna a e' il bit 0 di ogni byte, quindi uno spostamento a destra di 1 o 2 bit va verso la colonna a.
static inline U64 knight_fill(U64 b) {
  const U64 l1 = (b >> 1) & 0x7F7F7F7F7F7F7F7FULL, l2 = (b >> 2) & 0x3F3F3F3F3F3F3F3FULL;
  const U64 r1 = (b << 1) & 0xFEFEFEFEFEFEFEFEULL, r2 = (b << 2) & 0xFCFCFCFCFCFCFCFCULL;
  const U64 h1 = l1 | r1, h2 = l2 | r2;
  return (h1 << 16) | (h1 >> 16) | (h2 << 8) | (h2 >> 8);
}


// Punteggi delle quiete: statistiche (pesate), bonus agli scacchi diretti che non perdono materiale, e la mossa
// di un pezzo che esce da (o entra in) una casa attaccata da un pezzo nemico di valore minore.
static void score_quiets(MoveQueue &q, const int *mv, int *sc, int n) {
  PROF_GUARD(prof_score);
  ThreadData &td = *q.td;
  NodeFrame *f = q.f;
  const int us = td.side, them = us ^ 1;
  const int eoff = them == white ? 0 : 6, us_off = 6 - eoff;
  const U64 occ = td.occupancies[both];

  // Case attaccate da tutti i pedoni avversari insieme: gli stessi due shift di mask_pawn_attacks (attacks.cpp)
  // applicati alla bitboard intera, quindi la stessa unione del ciclo per pedone (04/10/2026).
  const U64 epawns = td.bitboards[P + eoff];
  const U64 by_pawn = them == white ? ((epawns >> 7) & not_a_file) | ((epawns >> 9) & not_h_file)
                                    : ((epawns << 7) & not_h_file) | ((epawns << 9) & not_a_file);
  // AA2 (10/10/2026, velocita', stessi punteggi): mappe dell'avversario e attacchi da casa sicura (AttackOrder*) da
  // UN calcolo DualMagic per pezzo lungo nemico (get_slider_pair, AA1), invece dei quattro cicli by_* e dei cinque cicli
  // di attack_to, che ricalcolavano gli attacchi degli stessi pezzi (alfiere, torre e donna di nuovo, due volte per la
  // donna). Contatori (bench 15 su fens30): 0,16 riempimenti per nodo, 0,84 calcoli per nodo prima, ~0,35 ora; nove
  // cicli a lunghezza variabile per riempimento diventano tre (piu' corti), cavalli e pedoni passano per insiemi.
  // Primo passo, sui pezzi lunghi: by_bishop, by_rook, by_queen e le parti di attack_to che non dipendono da `safe`
  // (le torri e la donna sono sempre bersagli per l'alfiere, la donna per la torre).
  const U64 eN = td.bitboards[N + eoff], eB = td.bitboards[B + eoff], eR = td.bitboards[R + eoff],
            eQ = td.bitboards[Q + eoff];
  const U64 by_knight = knight_fill(eN);
  U64 by_bishop = 0, by_rook = 0, by_queen = 0;
  U64 aa_b = 0, aa_r = 0, aa_q = 0;            // attack_to[B], [R], [Q] (vedi sotto per i bersagli)
  U64 sl_b[16], sl_r[16];                      // attacchi di alfiere e di torre dei pezzi lunghi, per il secondo passo
  unsigned char sl_slot[64];                   // indice in sl_b/sl_r per casa (scritto solo per i pezzi lunghi)
  int nsl = 0;
  for (U64 t = eB | eR | eQ; t; pop_lsb_bb(t)) {
    const int sq = get_ls1b_index(t);
    const SliderPair sp = get_slider_pair(sq, occ);
    const U64 b = pair_bishop(sp), r = pair_rook(sp);
    const U64 mB = 0 - ((eB >> sq) & 1), mR = 0 - ((eR >> sq) & 1), mQ = 0 - ((eQ >> sq) & 1);
    by_bishop |= b & mB;
    by_rook |= r & mR;
    by_queen |= (b | r) & mQ;
    aa_b |= b & (mR | mQ);
    aa_r |= r & mQ;
    sl_b[nsl] = b;
    sl_r[nsl] = r;
    sl_slot[sq] = (unsigned char)nsl++;
  }
  const U64 by_minor = by_knight | by_bishop;
  // Mappa per see_holds_q (la donna e il re non entrano in lesser[], solo qui).
  q.opp_slider = by_bishop | by_rook | by_queen;
  q.opp_att = by_pawn | by_knight | q.opp_slider | king_attacks[get_ls1b_index(td.bitboards[K + eoff])];
  q.opp_ready = true;
  U64 lesser[6];
  lesser[P] = 0;
  lesser[N] = lesser[B] = by_pawn;
  lesser[R] = by_minor | by_pawn;
  lesser[Q] = by_rook | lesser[R];
  lesser[K] = 0;
  // ThreatHist: threatened squares per piece type (09_history.inc): the lesser[] maps, plus pawn attacks for pawns.
  if (ThreatHist) {
    for (int k = P; k <= K; k++) q.menace[k] = lesser[k];
    q.menace[P] = by_pawn;
    q.menace_ready = true;
  }

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

  // Avamposti (08/10/2026, utente): il cavallo che va su una casa della meta' avversaria (traverse 4-6 dal nostro
  // lato), difesa da un nostro pedone e che nessun pedone avversario potra' mai attaccare (nessun pedone nemico davanti
  // sulle colonne vicine), si prova prima. OutpostSafe: 1 = in piu' l'avversario non ha alfieri del colore della casa,
  // 2 = in piu' non ha cavalli (la casa non si puo' contestare con un pezzo minore). Numerazione a8 = 0: il bianco
  // avanza verso indici piu' bassi.
  // Case davanti ai pedoni avversari, nella loro direzione di marcia (front), e case che quei pedoni potranno
  // attaccare avanzando (span). Un nostro pedone fuori da entrambe e' passato.
  U64 front = 0, span = 0;
  if (OutpostOrder || OutpostOrderB || PassedPush) {
    front = epawns;
    if (them == white) { front |= front >> 8; front |= front >> 16; front |= front >> 32; }
    else               { front |= front << 8; front |= front << 16; front |= front << 32; }
    span = them == white ? ((front >> 7) & not_a_file) | ((front >> 9) & not_h_file)
                         : ((front << 7) & not_h_file) | ((front << 9) & not_a_file);
  }
  U64 outpost = 0;
  if (OutpostOrder || OutpostOrderB) {
    const U64 ours_p = td.bitboards[P + us_off];
    const U64 guarded = us == white ? ((ours_p >> 7) & not_a_file) | ((ours_p >> 9) & not_h_file)
                                    : ((ours_p << 7) & not_h_file) | ((ours_p << 9) & not_a_file);
    const U64 rows = us == white ? 0x000000FFFFFF0000ULL : 0x0000FFFFFF000000ULL;   // traverse 4-6 / 5-3
    outpost = rows & guarded & ~span;
    if (OutpostSafe >= 1) {
      constexpr U64 light = 0xAA55AA55AA55AA55ULL;   // a8 (indice 0) e' chiara
      const U64 eb = td.bitboards[B + eoff];
      if (eb & light)  outpost &= ~light;
      if (eb & ~light) outpost &= light;
    }
    if (OutpostSafe >= 2 && td.bitboards[N + eoff])
      outpost = 0;
  }

  // Spinta del passato (08/10/2026): case d'arrivo dalla traversa relativa PassedPushRank alla settima dove un nostro
  // pedone e' passato. La SEE (>= 0) si chiede solo alle mosse di pedone che ci arrivano.
  U64 passed_to = 0;
  if (PassedPush) {
    U64 rows = 0;
    for (int rr = PassedPushRank; rr <= 7; rr++)
      rows |= 0xFFULL << (8 * (us == white ? 8 - rr : rr - 1));
    passed_to = rows & ~(front | span);
  }

  // Attacchi da casa sicura (08/10/2026; l'idea e' in Reckless, la regola e' nostra): per ogni tipo di pezzo, le case
  // non attaccate dall'avversario da cui quel pezzo attaccherebbe un bersaglio. Bersagli: pezzi nemici (non pedoni,
  // non il re) di classe piu' alta (pedone < cavallo = alfiere < torre < donna), o della stessa classe o piu' bassa
  // ma non difesi. Le case si trovano al contrario, dagli attacchi del bersaglio (pedone: quelli di un pedone nemico).
  U64 attack_to[6] = {0, 0, 0, 0, 0, 0};
  if (AttackOrder || AttackOrderQ || ThreatBal) {   // ThreatBal: le stesse mappe, per il valore minacciato
    const U64 safe = ~q.opp_att;
    const U64 minors = eN | eB;
    // AA2, secondo passo. Bersagli: dell'alfiere er|eq|(minors&safe), della torre eq|((minors|er)&safe), della donna
    // (minors|er)&safe, del cavallo come l'alfiere, del pedone minors|er|eq. Le parti senza `safe` sono gia' in aa_b e
    // aa_r; alfieri e torri al sicuro riusano i loro attacchi del primo passo; i cavalli al sicuro (0-2) sono gli
    // unici bersagli che chiedono un calcolo nuovo. Cavalli e pedoni come insiemi (knight_fill, shift dei pedoni).
    for (U64 t = (eB | eR) & safe; t; pop_lsb_bb(t)) {
      const int sq = get_ls1b_index(t), k = sl_slot[sq];
      aa_b |= sl_b[k] & (0 - ((eB >> sq) & 1));
      aa_r |= sl_r[k];
      aa_q |= sl_b[k] | sl_r[k];
    }
    for (U64 t = eN & safe; t; pop_lsb_bb(t)) {
      const SliderPair sp = get_slider_pair(get_ls1b_index(t), occ);
      aa_b |= pair_bishop(sp);
      aa_r |= pair_rook(sp);
      aa_q |= pair_bishop(sp) | pair_rook(sp);
    }
    if (AttackOrder || ThreatBal) {
      const U64 tp = minors | eR | eQ;   // gli attacchi di un pedone nemico posto su ogni bersaglio
      attack_to[P] = them == white ? ((tp >> 7) & not_a_file) | ((tp >> 9) & not_h_file)
                                   : ((tp << 7) & not_h_file) | ((tp << 9) & not_a_file);
      attack_to[N] = knight_fill(eR | eQ | (minors & safe));
      attack_to[B] = aa_b;
      attack_to[R] = aa_r;
    }
    if (AttackOrderQ || ThreatBal)
      attack_to[Q] = aa_q;
#ifdef TRIUMV_VERIFY_AA2   // verifica: i cicli di prima (08/10/2026) devono dare le stesse mappe
    {
      const U64 er = eR, eq = eQ;
      U64 old[6] = {0, 0, 0, 0, 0, 0}, ok = 0, ob = 0, orr = 0, oq = 0;
      for (U64 t = eN; t; pop_lsb_bb(t)) ok |= knight_attacks[get_ls1b_index(t)];
      for (U64 t = eB; t; pop_lsb_bb(t)) ob |= get_bishop_attacks(get_ls1b_index(t), occ);
      for (U64 t = eR; t; pop_lsb_bb(t)) orr |= get_rook_attacks(get_ls1b_index(t), occ);
      for (U64 t = eQ; t; pop_lsb_bb(t)) oq |= get_queen_attacks(get_ls1b_index(t), occ);
      if (AttackOrder) {
        for (U64 t = minors | er | eq; t; pop_lsb_bb(t)) old[P] |= pawn_attacks[them][get_ls1b_index(t)];
        for (U64 t = er | eq | (minors & safe); t; pop_lsb_bb(t)) old[N] |= knight_attacks[get_ls1b_index(t)];
        for (U64 t = er | eq | (minors & safe); t; pop_lsb_bb(t)) old[B] |= get_bishop_attacks(get_ls1b_index(t), occ);
        for (U64 t = eq | ((minors | er) & safe); t; pop_lsb_bb(t)) old[R] |= get_rook_attacks(get_ls1b_index(t), occ);
      }
      if (AttackOrderQ)
        for (U64 t = (minors | er) & safe; t; pop_lsb_bb(t)) old[Q] |= get_queen_attacks(get_ls1b_index(t), occ);
      bool same = ok == by_knight && ob == by_bishop && orr == by_rook && oq == by_queen;
      for (int k = P; k <= Q; k++) same = same && old[k] == attack_to[k];
      if (!same) {
        printf("info string AA2 SBAGLIATA: mappe d'attacco diverse\n");
        fflush(stdout);
        abort();
      }
    }
#endif
    // AttackSafeSee 0: casa d'arrivo non attaccata da nessun pezzo nemico. 1: non attaccata da pezzi di valore minore
    // (lesser[]), e lo scambio sulla casa si controlla mossa per mossa con la SEE (sotto).
    for (int k = P; k <= Q; k++) attack_to[k] &= AttackSafeSee ? ~lesser[k] : safe;
  }
  const int attack_bonus[6] = {AttackOrder, AttackOrder, AttackOrder, AttackOrder, AttackOrderQ, 0};

  // ---- Per-expert weights of the quiet-move score (block 1; docs/development/8.1/SEARCH_1_ORDERING.md) ----
  // tb_e = expert of the node (net_phase), shared by every weight below.
  //
  // ThreatBal (ours, off; package lost -17 at 8+0.08 with a hand-set share): one threat balance in units of material
  // in place of the fixed threat term and the safe-square attack bonuses,
  //   value saved - value exposed + ThreatBalGain/1024 * value of the best target attacked from a safe square,
  // times a weight per expert. Threatened = the lesser[] map; targets and safe squares as in attack_to[]. Off: the
  // tree is the old one.
  const int tb_e = net_phase(count_bits(occ));
  const int tb_w = tb_e == 0 ? ThreatBalW0 : tb_e == 1 ? ThreatBalW1 : tb_e == 2 ? ThreatBalW2 : ThreatBalW3;
  // A. Reading the histories (HistMix), /1024: main history, pawn history, near continuations (1, 2 plies back), far
  //    ones (3, 4, 6), first-ply history, and the agreement term (ours): when cont1 and cont2 agree in sign, HmAgree/1024
  //    of the smaller one is added. Defaults (2048, 2048, 1024, 1024, 1024, 0) give exactly the old sum
  //    2*main + 2*pawn + cont1..cont6 + first plies (checked by the bench, 16/10/2026; old formula removed).
  struct HmW { int main, pawn, near_, far_, low, agree; };
  const HmW hm = tb_e == 0 ? HmW{HmMain0, HmPawn0, HmNear0, HmFar0, HmLow0, HmAgree0}
               : tb_e == 1 ? HmW{HmMain1, HmPawn1, HmNear1, HmFar1, HmLow1, HmAgree1}
               : tb_e == 2 ? HmW{HmMain2, HmPawn2, HmNear2, HmFar2, HmLow2, HmAgree2}
                           : HmW{HmMain3, HmPawn3, HmNear3, HmFar3, HmLow3, HmAgree3};
  // B. Checks: bonus and exchange threshold of a direct check per expert (defaults = the old fixed 13323 and -75).
  //    ChkDisc (ours, off): bonus for a quiet move that discovers a check: the piece is in checks.disc (between one of
  //    our sliders and the enemy king) and lands off the line to the king (legalgen::line_bb). Skipped at 0.
  const int chk_bonus = by_exp4(tb_e, ChkBonus0, ChkBonus1, ChkBonus2, ChkBonus3);
  const int chk_see = by_exp4(tb_e, ChkSee0, ChkSee1, ChkSee2, ChkSee3);
  const int chk_disc = by_exp4(tb_e, ChkDisc0, ChkDisc1, ChkDisc2, ChkDisc3);
  const int eksq = get_ls1b_index(td.bitboards[K + eoff]);
  // B. Threats: leaving and entering a square attacked by a lower-value piece, separate weights per expert (one weight,
  //    ThreatOrderMul, served both; defaults give the old term). ThrSafe (ours, off): extra bonus when the threatened
  //    piece escapes to a square no enemy piece attacks (q.opp_att). Skipped at 0.
  const int thr_esc = by_exp4(tb_e, ThrEsc0, ThrEsc1, ThrEsc2, ThrEsc3);
  const int thr_ent = by_exp4(tb_e, ThrEnt0, ThrEnt1, ThrEnt2, ThrEnt3);
  const int thr_safe = by_exp4(tb_e, ThrSafe0, ThrSafe1, ThrSafe2, ThrSafe3);
  const U64 tb_safe = ~q.opp_att, tb_minors = eN | eB;
  // ThreatBal targets per piece type, same rules as attack_to[]: higher-class pieces, or undefended ones.
  const U64 tb_targets[6] = {tb_minors | eR | eQ, eR | eQ | (tb_minors & tb_safe), eR | eQ | (tb_minors & tb_safe),
                             eQ | ((tb_minors | eR) & tb_safe), (tb_minors | eR) & tb_safe, 0};
  auto tb_target_value = [&](int pt, int from, int to) {
    const U64 o = occ ^ (1ULL << from);
    U64 a = pt == P ? pawn_attacks[us][to] : pt == N ? knight_attacks[to] : pt == B ? get_bishop_attacks(to, o)
          : pt == R ? get_rook_attacks(to, o) : get_queen_attacks(to, o);
    a &= tb_targets[pt];
    return (a & eQ) ? unit_value[Q] : (a & eR) ? unit_value[R] : (a & eB) ? unit_value[B] : (a & eN) ? unit_value[N] : 0;
  };

  const PieceToRow &c1 = *f[-1].cont_hist, &c2 = *f[-2].cont_hist, &c3 = *f[-3].cont_hist,
                   &c4 = *f[-4].cont_hist, &c6 = *f[-6].cont_hist;
  const PieceToRow &prow = pawn_row(td);
  const int ply = f->ply;
  // A. First-ply history weight: 8 x entry x 1024 / (1024 + ply x LpDecay), per expert; LpDecay = 1024 is the old
  //    8 x entry / (1 + ply), same integer division. The divisor is constant in the node, so the division is a
  //    multiplication by the 40-bit reciprocal M = ceil(2^40 / D), computed once here (a per-move idiv cost ~1% of
  //    cycles, AA2 10/10/2026). Exact while |numerator| < 2^40 / D: numerator <= 8 x HistLimQuiet x 1024 (5.9e7 at the
  //    default 7183), D <= 1024 + 4 x 3072 = 13312 -> limit 8.3e7, i.e. HistLimQuiet up to ~10,000 (beyond that, lower
  //    the maximum of LpDecay).
  const unsigned long long lp_den = 1024ULL + (unsigned long long)ply * by_exp4(tb_e, LpDecay0, LpDecay1, LpDecay2, LpDecay3);
  const unsigned long long lp_recip = ply < LOW_PLY_SIZE ? ((1ULL << 40) + lp_den - 1) / lp_den : 0;

  for (int i = 0; i < n; i++) {
    const int m = mv[i];
    const int pc = move_piece(m), to = move_to(m), from = move_from(m), pt = pc - us_off;   // our move
    const int key = quiet_key(m);
    const int hctx = (ThreatHist && !get_move_promoted(m))
                         ? (int)((q.menace[pt] >> from) & 1) | (int)((q.menace[pt] >> to) & 1) << 1 : 0;
    const int v1 = c1[pc][to], v2 = c2[pc][to];
    long long hs = (long long)hm.main * qh_order(td, us, key, hctx) + (long long)hm.pawn * prow[pc][to] +
                   (long long)hm.near_ * (v1 + v2) + (long long)hm.far_ * (c3[pc][to] + c4[pc][to] + c6[pc][to]);
    if ((v1 ^ v2) >= 0)   // same sign (zero included): the agreement is the smaller one in absolute value
      hs += (long long)hm.agree * (v1 >= 0 ? std::min(v1, v2) : std::max(v1, v2));
    int s = (int)(hs / 1024);
    if (((checks.chk[pt] >> to) & 1) && see_holds_q(td, q, m, chk_see))
      s += chk_bonus;
    if (chk_disc && ((checks.disc >> from) & 1) && !((legalgen::line_bb[from][eksq] >> to) & 1))
      s += chk_disc;
    if (ThreatBal) {
      int bal = unit_value[pt] * (int((lesser[pt] >> from) & 1) - int((lesser[pt] >> to) & 1));
      if ((attack_to[pt] >> to & 1) && (!AttackSafeSee || see_holds_q(td, q, m, 0)))
        bal += tb_target_value(pt, from, to) * ThreatBalGain / 1024;
      s += bal * tb_w;
    }
    else {
      const int from_t = int((lesser[pt] >> from) & 1), to_t = int((lesser[pt] >> to) & 1);
      s += unit_value[pt] * (thr_esc * from_t - thr_ent * to_t);
      if (thr_safe && from_t && !((q.opp_att >> to) & 1))
        s += unit_value[pt] * thr_safe;
    }
    if (ply < LOW_PLY_SIZE) {
      const int x = 8 * td.low_ply_hist[ply][key];
      const unsigned long long ax = (unsigned long long)(x < 0 ? -x : x) * 1024;
      const int lq = (int)((ax * lp_recip) >> 40);
#ifdef TRIUMV_VERIFY_AA2
      if ((x < 0 ? -lq : lq) != (long long)x * 1024 / (long long)lp_den) { printf("info string AA2 WRONG: low ply\n"); fflush(stdout); abort(); }
#endif
      s += (x < 0 ? -lq : lq) * hm.low / 1024;
    }
    if (shield >> from & 1)
      s -= KingShield;
    if (pt == N && (outpost >> to & 1))
      s += OutpostOrder;
    if (pt == B && (outpost >> to & 1))
      s += OutpostOrderB;
    if (!ThreatBal && (attack_to[pt] >> to & 1) && (!AttackSafeSee || see_holds_q(td, q, m, 0)))
      s += attack_bonus[pt];
    if (pt == P && (passed_to >> to & 1) && see_holds_q(td, q, m, 0))
      s += PassedPush;
    sc[i] = s;
  }
}

// 05/10/2026 — FUORI DA queue_next (noinline). Incorporate, le tre fill portavano in queue_next le loro liste (mosse
// del generatore, punteggi, chiavi dell'ordinamento): frame da 5.048 byte, oltre la pagina da 4 KB, e su Windows ogni
// chiamata di queue_next, cioe' una per mossa, passava dalla sonda dello stack (__chkstk). Le fill girano una volta
// per fase del nodo: il loro frame resta sotto i 4 KB e la sonda sparisce (verificato nel disassemblato PGO).
// xperf 6 giri, release PGO avx512, 57,8 M nodi identici: istruzioni -2,76%, cicli -0,81%; insieme alle liste della
// valutazione fuori dallo stack (AccumulatorStack::BothLists) cicli -1,55%.
__attribute__((noinline)) static void fill_tactics(MoveQueue &q) {
  ThreadData &td = *q.td;
  moves ml[1];
  int sc[256];
  if (td_lists_legal())
    ml->count = legal_generate_stage<legalgen::TACTICS>(td, q.lgen, ml->moves);
  else
    td_generate_moves(td, ml, /*captures_only=*/true, /*quiets_only=*/false, /*known_in_check=*/0,
                      /*promo_quiet=*/true, /*promo_queen_only=*/true);
  // La mossa hash e' gia' uscita (QS_HASH): si toglie qui compattando sul posto, senza salti, e i cicli di
  // queue_next non la confrontano piu' a ogni mossa (04/10/2026 sera; stesso ordine delle altre, stesso albero).
  int n = 0;
  for (int i = 0; i < ml->count; i++) {
    const int m = ml->moves[i];
    ml->moves[n] = m;
    n += m != q.hash_move;
  }
  q.ex = net_phase(count_bits(td.occupancies[both]));   // expert of the node: capture weights and group thresholds
  q.good_capt = by_exp4(q.ex, StGood0, StGood1, StGood2, StGood3);
  for (int i = 0; i < n; i++)
    sc[i] = score_tactic(td, ml->moves[i], q.ex);
  q.n_tact = q.n_all = n;
  rank_into<false>(q.list, ml->moves, sc, n, INT_MIN);
  q.cur = 0;
}

__attribute__((noinline)) static void fill_quiets(MoveQueue &q) {
  ThreadData &td = *q.td;
  moves ml[1];
  int sc[256];
  if (td_lists_legal())
    ml->count = legal_generate_stage<legalgen::QUIETS>(td, q.lgen, ml->moves);
  else
    td_generate_moves(td, ml, /*captures_only=*/false, /*quiets_only=*/true, /*known_in_check=*/0);
  // La promozione a donna e' gia' fra le tattiche e la mossa hash e' gia' uscita: si tolgono compattando sul posto,
  // senza salti.
  int n = 0;
  for (int i = 0; i < ml->count; i++) {
    const int m = ml->moves[i];
    ml->moves[n] = m;
    n += (promo_kind(m) != Q) & (m != q.hash_move);
  }
  q.n_all = q.n_tact + n;
  score_quiets(q, ml->moves, sc, n);
  if (LmrRank) {   // LmrRank: il punteggio piu' alto fra le quiete (anche fra quelle sotto il limite, non ordinate)
    int top = n ? sc[0] : 0;
    for (int i = 1; i < n; i++)
      top = std::max(top, sc[i]);
    q.quiet_top = top;
  }
  // D. Expert already chosen by fill_tactics (quiet moves always come after captures, same position): StSort sets how
  //    many quiet moves are sorted, StQuiet the boundary between good and bad ones.
  const int limit = -by_exp4(q.ex, StSort0, StSort1, StSort2, StSort3) * q.depth;
  const int gq = q.good_quiet = by_exp4(q.ex, StQuiet0, StQuiet1, StQuiet2, StQuiet3);
  const int nh = rank_into<false>(q.list + q.n_tact, ml->moves, sc, n, limit);
  q.cur = q.n_tact;
  // Good quiet moves (score > gq) leave in QS_QUIET_HIGH, the others in QS_QUIET_LOW, each in list order. When
  // limit <= gq + 1 (depth >= 5 at the starting values) the unsorted tail has scores < limit, so all <= gq, and the
  // sorted head is decreasing: the good moves are exactly a prefix of the list. The boundary is found once and the two
  // stages become two contiguous ranges, same output order (V2, 08/10/2026, speed at an identical tree). At low depth
  // the stages scan the list and compare each score.
  if (limit <= gq + 1) {
    int b = 0;
    while (b < nh && q.list[q.n_tact + b].score > gq)
      b++;
    q.quiet_split = q.n_tact + b;
#ifdef TRIUMV_VERIFY_QSPLIT   // verifica: prima del confine solo buone, dopo solo cattive (stesso ordine di prima)
    for (int i = q.n_tact; i < q.n_all; i++)
      if ((q.list[i].score > gq) != (i < q.quiet_split)) {
        printf("info string QSPLIT SBAGLIATO: indice %d punteggio %d confine %d\n", i, q.list[i].score, q.quiet_split);
        fflush(stdout);
        abort();
      }
#endif
  }
}

__attribute__((noinline)) static void fill_evasions(MoveQueue &q) {
  ThreadData &td = *q.td;
  moves ml[1];
  int sc[256];
  if (td_lists_legal())
    ml->count = legal_generate_stage<legalgen::ALL>(td, q.lgen, ml->moves);
  else
    td_generate_moves(td, ml, false, false, /*known_in_check=*/1);
  int n = 0;   // senza la mossa hash, gia' uscita (vedi fill_tactics)
  for (int i = 0; i < ml->count; i++) {
    const int m = ml->moves[i];
    ml->moves[n] = m;
    n += m != q.hash_move;
  }
  const PieceToRow &c1 = *q.f[-1].cont_hist;
  // Le tattiche davanti a tutte le quiete: 1 << 20 basta (|quiete| <= LIM_QUIET + LIM_FOLLOW = 37.183) e sta nei 22
  // bit delle chiavi a 32 bit; prima 1 << 28 con chiavi a 64 bit (frame di fill_evasions oltre i 4 KB). Stesso
  // ordine, stesso albero (05/10/2026).
  // C. Quiet evasions: main history and first continuation weighted per expert (HeMain, HeCont, /1024; defaults give
  //    the old sum). With weights up to 2048 a quiet move stays below 1 << 20 (|history| + |cont1| <= 37,183 x 2), so
  //    captures (+ 1 << 20) always come first.
  const int eex = q.ex = net_phase(count_bits(td.occupancies[both]));
  const int he_main = by_exp4(eex, HeMain0, HeMain1, HeMain2, HeMain3), he_cont = by_exp4(eex, HeCont0, HeCont1, HeCont2, HeCont3);
  for (int i = 0; i < n; i++) {
    const int m = ml->moves[i];
    sc[i] = is_tactical(m) ? unit_value[victim_kind(td, m)] + (1 << 20)
                           : (he_main * qh_order(td, td.side, quiet_key(m), quiet_ctx(q, m)) +
                              he_cont * c1[move_piece(m)][move_to(m)]) / 1024;
  }
  q.n_all = q.n_tact = n;
  rank_into<false>(q.list, ml->moves, sc, n, INT_MIN);
  q.cur = 0;
}

// GL (15/10/2026): la mossa m, uscita dalla coda, viene da una lista del generatore legale e non dalla tabella hash:
// e' legale senza legal_before ne' test del re nella make. In Chess960 sempre false.
static inline bool queue_move_legal(const MoveQueue &q, int m) { return td_lists_legal() && m != q.hash_move; }

// T1 (15/10/2026, velocita' ad albero identico): quando la coda consegna una mossa da una lista gia' ordinata, si
// anticipa il bucket hash del figlio della mossa DOPO (chiave stimata, come la
// make). La probe del figlio aspettava la memoria anche col prefetch della make, che parte poche centinaia di cicli
// prima; questo parte un intero sottoalbero prima, e la maggior parte dei sottoalberi e' piccola (quiescenza, profondita'
// basse). Solo prefetch: nessun effetto sull'albero. La mossa sbirciata puo' non essere quella che esce (SEE delle
// tattiche, fasi successive): allora il prefetch va a vuoto.
// T1v (15/10/2026): il prefetch della mossa dopo paga su Intel (i9-14900KF, un motore per core: +1,1/+1,25%) ma costa su
// AMD (benchnet, nodi identici: Ryzen 7 2700X -1,4%, Ryzen 7 5700 -1,6% con un solo motore, -4,2% con sette insieme).
// La build avx2 serve entrambi, quindi si decide una volta all'avvio dalla stringa del produttore (CPUID foglia 0).
// Fuori da x86 (ARM: Apple, Android) resta spento, come prima di T1: non e' stato misurato. Provata e scartata una
// versione che anticipa solo le mosse che stanno per essere giocate (niente quiescenza, ProbCut, prima mossa dei nodi
// cut; prefetch utili dal 29% a ~75%): Intel -0,27% [-0,71, +0,16], AMD ancora negativo (2700X -0,9%, 5700 -0,6%).
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
  #if defined(_MSC_VER)
    #include <intrin.h>
  #else
    #include <cpuid.h>
  #endif
#endif
static bool cpu_vendor_intel() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
  unsigned b = 0, c = 0, d = 0;
  #if defined(_MSC_VER)
  int x[4];
  __cpuid(x, 0);
  b = (unsigned)x[1], c = (unsigned)x[2], d = (unsigned)x[3];
  #else
  unsigned a = 0;
  __get_cpuid(0, &a, &b, &c, &d);
  #endif
  return b == 0x756e6547u && d == 0x49656e69u && c == 0x6c65746eu;   // "Genu" "ineI" "ntel"
#else
  return false;
#endif
}
// 15/10/2026: SPENTO nelle build AVX-512 (avx512, vnni512, avx512icl). Xeon Gold 6138 a due socket, variante avx512,
// PGO, xperf a motore fissato, 4 giri: senza prefetch -0,40% cicli/nodo mediogioco e -0,72% finali rispetto alla PR
// completa (rumore A/A 0,28% / 0,19%), salti mal predetti di nuovo al livello della base (DEVELOPMENT_8.1 §1). Resta
// acceso dove e' stato misurato: Intel senza AVX-512 (12a-14a generazione) nelle build avx2, avx2-nopext, avxvnni.
#if defined(USE_AVX512)
static const bool g_next_prefetch = false;
#else
static const bool g_next_prefetch = cpu_vendor_intel();
#endif

static inline void queue_prefetch(const MoveQueue &q, int m) {
  if (g_next_prefetch)
    td_prefetch_child(*q.td, td_child_key_guess(*q.td, m));
}

// Prossima mossa pseudo-legale, 0 a coda finita. La mossa hash esce una volta sola.
static int queue_next(MoveQueue &q) {
  ThreadData &td = *q.td;
  q.rank_ok = false;   // LmrRank: lo rimettono a true solo le uscite delle quiete ordinate (sotto)
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
        // D. Good capture: SEE >= -score x StGood / (19 x 1024); StGood = 1024 is the old -score/19, same truncation.
        if (see_holds(td, sm.move, (int)(-(long long)sm.score * q.good_capt / 19456))) {
          if (q.cur < q.n_tact)
            queue_prefetch(q, q.list[q.cur].move);
          return sm.move;
        }
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
      if (q.quiet_split >= 0) {   // V2: le buone sono il prefisso [n_tact, quiet_split)
        if (!q.quiets_off && q.cur < q.quiet_split) {
          const ScoredMove sm = q.list[q.cur++];
          if (q.cur < q.n_all)
            queue_prefetch(q, q.list[q.cur].move);
          q.rank_ok = true, q.last_score = sm.score;
          return sm.move;
        }
      } else
        while (!q.quiets_off && q.cur < q.n_all) {
          const ScoredMove sm = q.list[q.cur++];
          if (sm.score > q.good_quiet) {
            q.rank_ok = true, q.last_score = sm.score;
            return sm.move;
          }
        }
      q.step = QS_TACT_LOSE;
      break;

    case QS_TACT_LOSE:
      if (q.cur_lose < q.n_lose) {
        const int m = q.list[q.losers[q.cur_lose++]].move;
        if (q.cur_lose < q.n_lose)
          queue_prefetch(q, q.list[q.losers[q.cur_lose]].move);
        return m;
      }
      q.cur = q.quiet_split >= 0 ? q.quiet_split : q.n_tact;
      q.step = QS_QUIET_LOW;
      break;

    case QS_QUIET_LOW:
      if (q.quiet_split >= 0) {   // V2: le cattive sono il resto [quiet_split, n_all), gia' nell'ordine giusto
        if (!q.quiets_off && q.cur < q.n_all) {
          const ScoredMove sm = q.list[q.cur++];
          if (q.cur < q.n_all)
            queue_prefetch(q, q.list[q.cur].move);
          q.rank_ok = true, q.last_score = sm.score;
          return sm.move;
        }
      } else
        while (!q.quiets_off && q.cur < q.n_all) {
          const ScoredMove sm = q.list[q.cur++];
          if (sm.score <= q.good_quiet) {
            q.rank_ok = true, q.last_score = sm.score;
            return sm.move;
          }
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
        if (q.kind == QK_CUTS && !see_holds(td, sm.move, q.cut_threshold))
          continue;
        if (q.cur < q.n_tact)
          queue_prefetch(q, q.list[q.cur].move);
        return sm.move;
      }
      q.step = QS_DONE;
      break;

    default:
      return 0;
    }
  }
}
