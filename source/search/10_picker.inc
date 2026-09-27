// search/10_picker.inc -- Move picker a stadi, ripetizioni, cuckoo.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// STAGED MOVE PICKER  (UCI option "MovePicker", default OFF)
// ============================================================================

// Pseudo-legality validator. The staged picker yields the TT move, killers and
// the counter-move WITHOUT first generating the move list, so it must confirm
// each is a real move in THIS position: td_make_move trusts the encoded bits
// and would corrupt the board on a TT collision. This MUST NEVER return true
// for an illegal move; when unsure it returns false and the move simply
// reappears during normal generation (we only lose a little laziness, never
// correctness). Mirrors td_generate_moves.
static inline bool td_is_pseudo_legal(ThreadData &td, int move) {
  if (move == 0)
    return false;
  const int source = get_move_source(move);
  const int target = get_move_target(move);
  const int piece = get_move_piece(move);
  const int promoted = get_move_promoted(move);
  const int capture = get_move_capture(move) ? 1 : 0;
  const int dbl = get_move_double(move) ? 1 : 0;
  const int enpass = get_move_enpassant(move) ? 1 : 0;
  const int castling = get_move_castling(move) ? 1 : 0;

  // The moving piece must belong to the side to move and sit on `source`.
  if (td.side == white) {
    if (piece < P || piece > K)
      return false;
  } else {
    if (piece < p || piece > k)
      return false;
  }
  if (!get_bit(td.bitboards[piece], source))
    return false;

  const U64 occ = td.occupancies[both];
  const U64 friends = td.occupancies[td.side];
  const U64 enemies = td.occupancies[td.side ^ 1];

  // A target holding a friendly piece is never legal (also rejects
  // source==target).
  if (get_bit(friends, target))
    return false;

  const bool isPawn = (piece == P || piece == p);
  const bool isKing = (piece == K || piece == k);

  // ---- Castling ----
  if (castling) {
    if (!isKing || capture || promoted || enpass || dbl)
      return false;
    if (td.side == white) {
      if (source != e1)
        return false;
      if (target == g1)
        return (td.castle & wk) && !get_bit(occ, f1) && !get_bit(occ, g1) &&
               !td_is_square_attacked(td, e1, black) &&
               !td_is_square_attacked(td, f1, black);
      if (target == c1)
        return (td.castle & wq) && !get_bit(occ, d1) && !get_bit(occ, c1) &&
               !get_bit(occ, b1) && !td_is_square_attacked(td, e1, black) &&
               !td_is_square_attacked(td, d1, black);
      return false;
    } else {
      if (source != e8)
        return false;
      if (target == g8)
        return (td.castle & bk) && !get_bit(occ, f8) && !get_bit(occ, g8) &&
               !td_is_square_attacked(td, e8, white) &&
               !td_is_square_attacked(td, f8, white);
      if (target == c8)
        return (td.castle & bq) && !get_bit(occ, d8) && !get_bit(occ, c8) &&
               !get_bit(occ, b8) && !td_is_square_attacked(td, e8, white) &&
               !td_is_square_attacked(td, d8, white);
      return false;
    }
  }

  // Only pawns may carry promotion / double-push / en-passant flags.
  if (!isPawn && (promoted || dbl || enpass))
    return false;

  // ---- Pawns ----
  if (isPawn) {
    const int up = (td.side == white) ? -8 : 8; // board layout: a8=0 ... h1=63
    const bool toLastRank =
        (td.side == white) ? (target <= h8) : (target >= a1);

    // A pawn reaching the last rank MUST promote (and the promoted piece must
    // belong to the side to move); promotion off the last rank is illegal.
    if (promoted) {
      if (!toLastRank)
        return false;
      if (td.side == white) {
        if (promoted < N || promoted > Q)
          return false;
      } else {
        if (promoted < n || promoted > q)
          return false;
      }
    } else if (toLastRank && !enpass) {
      return false;
    }

    if (capture) {
      if (!get_bit(pawn_attacks[td.side][source], target))
        return false;
      if (enpass) {
        if (dbl)
          return false;
        if (td.enpassant == no_sq || target != td.enpassant)
          return false;
        const int capsq = (td.side == white) ? target + 8 : target - 8;
        const int eppawn = (td.side == white) ? p : P;
        return get_bit(td.bitboards[eppawn], capsq) != 0;
      }
      return get_bit(enemies, target) != 0; // ordinary capture
    } else {
      if (enpass)
        return false;
      if (get_bit(occ, target))
        return false; // push onto empty square only
      if (dbl) {
        const bool onStart = (td.side == white)
                                 ? (source >= a2 && source <= h2)
                                 : (source >= a7 && source <= h7);
        if (!onStart)
          return false;
        if (target != source + 2 * up)
          return false;
        return !get_bit(occ, source + up); // intermediate square empty
      }
      return (target == source + up);
    }
  }

  // ---- Knight / Bishop / Rook / Queen / King (non-castling) ----
  U64 att;
  switch (piece) {
  case N:
  case n:
    att = knight_attacks[source];
    break;
  case B:
  case b:
    att = get_bishop_attacks(source, occ);
    break;
  case R:
  case r:
    att = get_rook_attacks(source, occ);
    break;
  case Q:
  case q:
    att = get_queen_attacks(source, occ);
    break;
  case K:
  case k:
    att = king_attacks[source];
    break;
  default:
    return false;
  }
  if (!get_bit(att, target))
    return false;
  // The capture flag must agree with whether an enemy occupies the target
  // square.
  return (capture != 0) == (get_bit(enemies, target) != 0);
}

// Stage order matches the legacy score bands so the search is (near)
// byte-identical.
enum {
  MPS_TT = 0,        // hash move (validated)
  MPS_GEN_TACTICAL,  // generate captures + capture-promotions, score them
  MPS_GOOD_TACTICAL, // promotions + SEE>=0 captures, by score
  MPS_KILLER0,
  MPS_KILLER1,
  MPS_COUNTER,
  MPS_GEN_QUIET,    // generate all moves, keep & score the quiets
  MPS_QUIET,        // quiets by history, low to high consumed
  MPS_BAD_TACTICAL, // SEE<0 captures, by score
  MPS_DONE
};

// Sentinel placed in a score slot once that move has been yielded. Far below
// any real move score (good caps ~+7e5, bad caps ~-7e5), so it never collides.
static const int MP_CONSUMED = -2000000000;

struct MovePicker {
  bool staged;
  int stage;
  // 2C: lo scacco al nodo, gia' calcolato dalla search. Resta valido per tutta la vita del
  // picker: fra i due stage di generazione le mosse vengono fatte e disfatte, ma make/unmake
  // ripristina esattamente la posizione, quindi il nodo e' sempre lo stesso.
  int in_check;
  int tt_move, killer0, killer1, counter;
  moves *caps;
  int *cap_scores;
  int cap_n; // capture/promotion buffer
  moves *quiets;
  int *q_scores;
  int q_n;   // quiet buffer (legacy: all moves)
  int l_idx; // legacy pick-next cursor
  // Phase-2 skip flags: set by the search loop when a pruning condition is met
  // BEFORE the first quiet/bad-cap is even generated. mp_next() reads these and
  // jumps past the entire stage — zero generation cost for pruned nodes.
  bool skip_quiets;   // skip MPS_GEN_QUIET + MPS_QUIET + killers/counter
  bool skip_bad_caps; // skip MPS_BAD_TACTICAL
};

// caps/cap_scores: scratch buffers for the tactical stages (staged mode only).
// quiets/q_scores: in legacy mode these are the fully generated+scored move
// list; in staged mode they are reused as the quiet-stage buffer.
static inline void mp_init(MovePicker &mp, ThreadData &td, bool staged,
                           int tt_move, moves *caps, int *cap_scores,
                           moves *quiets, int *q_scores, int in_check) {
  mp.staged = staged;
  mp.in_check = in_check;   // 2C: lo passa la search, che l'ha gia' calcolato
  mp.tt_move = tt_move;
  mp.caps = caps;
  mp.cap_scores = cap_scores;
  mp.cap_n = 0;
  mp.quiets = quiets;
  mp.q_scores = q_scores;
  mp.q_n = 0;
  mp.l_idx = 0;
  mp.stage = MPS_TT;
  mp.skip_quiets = false;
  mp.skip_bad_caps = false;
  if (staged) {
    mp.killer0 = td.killer_moves[0][td.ply];
    mp.killer1 = td.killer_moves[1][td.ply];
    int prev = td.move_stack[td.ply];
    // counter=0 spegne di fatto TUTTO il meccanismo: lo stage refutation
    // (:3175) e il dedup
    // (:3189) sono gia' guardati da `mp.counter &&`.
    mp.counter =
        (g_countermove && prev)
            ? td.counter_moves[get_move_piece(prev)][get_move_target(prev)]
            : 0;
  } else {
    mp.killer0 = mp.killer1 = mp.counter = 0;
  }
}

// Returns the next move to search, 0 when exhausted.
static int mp_next(ThreadData &td, MovePicker &mp) {
  PROF_GUARD(prof_mp);
  // --- Legacy: pick-next over the fully scored move list (bit-identical to the
  //     original in-loop selection sort). ---
  if (!mp.staged) {
    moves *ml = mp.quiets;
    int *sc = mp.q_scores;
    int i = mp.l_idx;
    if (i >= ml->count)
      return 0;
    int best = i;
    for (int j = i + 1; j < ml->count; j++)
      if (sc[j] > sc[best])
        best = j;
    if (best != i) {
      int tm = ml->moves[i];
      ml->moves[i] = ml->moves[best];
      ml->moves[best] = tm;
      int ts = sc[i];
      sc[i] = sc[best];
      sc[best] = ts;
    }
    mp.l_idx = i + 1;
    return ml->moves[i];
  }

  // --- Staged ---
  switch (mp.stage) {
  case MPS_TT:
    mp.stage = MPS_GEN_TACTICAL;
    if (mp.tt_move && td_is_pseudo_legal(td, mp.tt_move))
      return mp.tt_move;
    [[fallthrough]];

  case MPS_GEN_TACTICAL:
    td_generate_moves(td, mp.caps, /*captures_only=*/true, /*quiets_only=*/false,
                      mp.in_check);   // 2C
    mp.cap_n = mp.caps->count;
    for (int i = 0; i < mp.cap_n; i++)
      mp.cap_scores[i] = td_score_move(td, mp.caps->moves[i], mp.tt_move);
    mp.stage = MPS_GOOD_TACTICAL;
    [[fallthrough]];

  case MPS_GOOD_TACTICAL:
    // Good tacticals score > 0 (promotions 750k, SEE>=0 captures ~700k+); bad
    // captures score ~-700k and are deferred to MPS_BAD_TACTICAL.
    for (;;) {
      int best = -1, bs = 0;
      for (int i = 0; i < mp.cap_n; i++) { // senza salti, come lo stadio quiet
        const int v = mp.cap_scores[i];
        const bool b = v > bs;
        bs = b ? v : bs;
        best = b ? i : best;
      }
      if (best < 0)
        break;
      int m = mp.caps->moves[best];
      mp.cap_scores[best] = MP_CONSUMED;
      if (m != mp.tt_move)
        return m; // tt already yielded
    }
    mp.stage = MPS_KILLER0;
    [[fallthrough]];

  case MPS_KILLER0:
    mp.stage = MPS_KILLER1;
    if (!mp.skip_quiets && mp.killer0 && mp.killer0 != mp.tt_move &&
        td_is_pseudo_legal(td, mp.killer0))
      return mp.killer0;
    [[fallthrough]];

  case MPS_KILLER1:
    mp.stage = MPS_COUNTER;
    if (!mp.skip_quiets && mp.killer1 && mp.killer1 != mp.tt_move &&
        mp.killer1 != mp.killer0 && td_is_pseudo_legal(td, mp.killer1))
      return mp.killer1;
    [[fallthrough]];

  case MPS_COUNTER:
    mp.stage = MPS_GEN_QUIET;
    if (!mp.skip_quiets && mp.counter && mp.counter != mp.tt_move &&
        mp.counter != mp.killer0 && mp.counter != mp.killer1 &&
        td_is_pseudo_legal(td, mp.counter))
      return mp.counter;
    [[fallthrough]];

  case MPS_GEN_QUIET:
    // 🔴 PromoQS: skip_quiets saltava l'INTERO stage, e le promozioni non-cattura
    // nascono proprio qui. Ma la search le esenta da OGNI potatura quiet
    // (is_quiet = !is_capture && !is_promotion, :7445), quindi cancellarle con lo
    // skip contraddice l'intenzione dichiarata del codice. Con PromoQS lo stage
    // viene generato lo stesso e sono le SOLE promozioni a sopravvivere: tutte le
    // altre quiete restano marcate CONSUMED dal filtro qui sotto.
    // 🔴 GUARDIA A COSTO QUASI NULLO (modo 3). Nel modo 1 qui si generava l'INTERA
    // lista quieta per tenerne le sole promozioni: una movegen completa in ogni
    // nodo dove scatta la futility, che e' quasi certamente la voce dominante del
    // +17% di albero misurato. Due bitboard e un test: se non c'e' un pedone in
    // settima con la casa davanti libera, non c'e' NIENTE da salvare e si salta lo
    // stage come prima. Il modo 2 non entra mai qui.
    if (mp.skip_quiets) {
      bool keep = false;
      if (g_promo_qs == 1) {
        keep = true;  // modo 1: comportamento gia' misurato, lasciato confrontabile
      } else if (g_promo_qs == 3 || g_promo_qs == 4) {
        const U64 empty = ~td.occupancies[both];
        keep = (td.side == white)
                 ? ((td.bitboards[P] & 0x000000000000FF00ULL) &&
                    ((td.bitboards[P] & 0x000000000000FF00ULL) >> 8) & empty)
                 : ((td.bitboards[p] & 0x00FF000000000000ULL) &&
                    ((td.bitboards[p] & 0x00FF000000000000ULL) << 8) & empty);
      }
      if (!keep) {
        mp.stage = MPS_BAD_TACTICAL;
        goto bad_tactical;
      }
    }
    // 2A: quiets_only. Prima era `false` = tutte le pseudo-legali, quindi ogni cattura veniva
    // rigenerata qui dopo essere gia' stata prodotta da MPS_GEN_TACTICAL, e subito buttata
    // marcandola CONSUMED. Node-identical: le catture non venivano rese comunque.
    td_generate_moves(td, mp.quiets, /*captures_only=*/false, /*quiets_only=*/true,
                      mp.in_check);   // 2C
    mp.q_n = mp.quiets->count;
    QuietCtx qctx;                               // NPS 25/09/2026: contesto una volta per nodo
    td_quiet_ctx_init(td, qctx, mp.tt_move);
    for (int i = 0; i < mp.q_n; i++) {
      int m = mp.quiets->moves[i];
      // Le catture le rendono gli stage tattici; TT/killer/counter sono gia' state rese ->
      // marcate CONSUMED per non ripeterle. Il test get_move_capture resta anche con
      // quiets_only: la lista ora non ne contiene, ma il costo e' nullo e toglierlo
      // renderebbe la funzione dipendente dalla modalita' del chiamante.
      if (get_move_capture(m) || m == mp.tt_move || m == mp.killer0 ||
          m == mp.killer1 || m == mp.counter ||
          (mp.skip_quiets && !get_move_promoted(m)))
        mp.q_scores[i] = MP_CONSUMED;
      else
        mp.q_scores[i] = td_score_quiet(td, qctx, m);
    }
    mp.stage = MPS_QUIET;
    [[fallthrough]];

  case MPS_QUIET: {
    // Phase-2: if LMP/futility flipped skip_quiets WHILE we were already in the
    // quiet stage, stop yielding the remaining quiets at once (the search would
    // prune every one of them anyway) and go straight to the bad captures.
    if (mp.skip_quiets) {
      mp.stage = MPS_BAD_TACTICAL;
      goto bad_tactical;
    }
    // NPS 25/09/2026 — selezione del massimo SENZA SALTI. Il profilo xperf campionato su
    // BranchMispredictions dava a questo stadio ~11% di tutti i mispredict del motore
    // (~3,7 per nodo): il confronto `score > best` salta in modo imprevedibile a ogni
    // nuovo massimo. Provata anche la via "ordina una volta" (insertion sort stabile
    // dopo 3 selezioni): meno istruzioni ma PIU' mispredict (l'uscita dal ciclo interno
    // e' casuale). La scelta con cmov non ha salti sul dato e costa ~2 istruzioni per
    // elemento. Le mosse gia' rese valgono MP_CONSUMED, che non supera mai bs iniziale
    // (confronto stretto): il test `!= MP_CONSUMED` era ridondante.
    // 🔑 NODI IDENTICI: stessa regola di prima, "il primo massimo per indice".
    {
      int best = -1, bs = MP_CONSUMED;
      const int *sc = mp.q_scores;
      for (int i = 0; i < mp.q_n; i++) {
        const int v = sc[i];
        const bool b = v > bs;
        bs = b ? v : bs;
        best = b ? i : best;
      }
      if (best >= 0) {
        mp.q_scores[best] = MP_CONSUMED;
        return mp.quiets->moves[best];
      }
    }
    mp.stage = MPS_BAD_TACTICAL;
    // fallthrough into bad-tactical
  }

  bad_tactical:
  case MPS_BAD_TACTICAL:
    if (mp.skip_bad_caps) {
      mp.stage = MPS_DONE;
      return 0;
    }
    for (;;) {
      int best = -1, bs = MP_CONSUMED;
      for (int i = 0; i < mp.cap_n; i++) { // senza salti; CONSUMED non supera mai bs
        const int v = mp.cap_scores[i];
        const bool b = v > bs;
        bs = b ? v : bs;
        best = b ? i : best;
      }
      if (best < 0)
        break;
      int m = mp.caps->moves[best];
      mp.cap_scores[best] = MP_CONSUMED;
      if (m != mp.tt_move)
        return m;
    }
    mp.stage = MPS_DONE;
    return 0;

  default:
    return 0;
  }
}

// ============================================================================
// REPETITION DETECTION
// ============================================================================

// ============================================================================
// UPCOMING REPETITION (P1.2, 2026-06-09) — cuckoo, Kervinck / SF has_game_cycle
// ============================================================================
// cuckoo_key[i] = piece_keys[pc][s1] ^ piece_keys[pc][s2] ^ side_key per OGNI
// mossa reversibile (pezzi non-pedone, coppie raggiungibili su board vuota;
// 3668 in tutto, come SF). cuckoo_move[i] = s1 | (s2 << 6). between_tbl[s1][s2]
// = case STRETTAMENTE tra s1 e s2 (0 per cavallo / case non allineate).
static U64 cuckoo_key[8192];
static int cuckoo_move[8192];
// (between_tbl definita piu' sopra, prima di td_generate_moves: serve anche
//  alle evasioni P2.3, non solo all'upcoming-repetition.)

static inline int cuckoo_h1(U64 h) { return (int)(h & 0x1FFF); }
static inline int cuckoo_h2(U64 h) { return (int)((h >> 16) & 0x1FFF); }

static void init_cuckoo() {
  memset(cuckoo_key, 0, sizeof(cuckoo_key));
  memset(cuckoo_move, 0, sizeof(cuckoo_move));

  for (int s1 = 0; s1 < 64; s1++)
    for (int s2 = 0; s2 < 64; s2++) {
      between_tbl[s1][s2] = 0;
      if (s1 == s2)
        continue;
      // Intersezione dei raggi con l'altra casa come unica occupazione =
      // case strettamente comprese (gli estremi si elidono da soli).
      if (get_rook_attacks(s1, 0ULL) & (1ULL << s2))
        between_tbl[s1][s2] =
            get_rook_attacks(s1, 1ULL << s2) & get_rook_attacks(s2, 1ULL << s1);
      else if (get_bishop_attacks(s1, 0ULL) & (1ULL << s2))
        between_tbl[s1][s2] = get_bishop_attacks(s1, 1ULL << s2) &
                              get_bishop_attacks(s2, 1ULL << s1);
    }

  static const int rev_pieces[10] = {N, B, R, Q, K, n, b, r, q, k};
  for (int pi = 0; pi < 10; pi++) {
    const int pc = rev_pieces[pi];
    for (int s1 = 0; s1 < 64; s1++) {
      U64 att;
      switch (pc % 6) { // 1=N 2=B 3=R 4=Q 5=K (mai pedoni)
      case 1:
        att = knight_attacks[s1];
        break;
      case 2:
        att = get_bishop_attacks(s1, 0ULL);
        break;
      case 3:
        att = get_rook_attacks(s1, 0ULL);
        break;
      case 4:
        att = get_queen_attacks(s1, 0ULL);
        break;
      default:
        att = king_attacks[s1];
        break;
      }
      for (int s2 = s1 + 1; s2 < 64; s2++) {
        if (!(att & (1ULL << s2)))
          continue;
        U64 key = piece_keys[pc][s1] ^ piece_keys[pc][s2] ^ side_key;
        int mv = s1 | (s2 << 6);
        int i = cuckoo_h1(key);
        while (true) { // inserimento cuckoo (2 sedi possibili)
          U64 tk = cuckoo_key[i];
          cuckoo_key[i] = key;
          key = tk;
          int tm = cuckoo_move[i];
          cuckoo_move[i] = mv;
          mv = tm;
          if (key == 0)
            break; // slot era vuoto: catena chiusa
          i = (i == cuckoo_h1(key)) ? cuckoo_h2(key) : cuckoo_h1(key);
        }
      }
    }
  }
}

// True se il lato al tratto ha una mossa reversibile che chiude un ciclo verso
// una posizione gia' vista nelle ultime `fifty` semimosse (port di SF
// has_game_cycle sulla nostra repetition_table piatta: [repetition_index] =
// posizione 1 ply fa, [repetition_index-1] = 2 ply fa, ...). Le entry create
// dalle NULL move sono inerti: la loro moveKey differisce solo per side/ep, mai
// per due piece-key, e non puo' combaciare con una chiave cuckoo -> nessun
// falso positivo.
static bool td_upcoming_repetition(ThreadData &td) {
  int end = td.fifty;
  if (end > td.repetition_index)
    end = td.repetition_index;
  if (end < 3)
    return false;
  const U64 orig = td.hash_key;
  for (int i = 3; i <= end; i += 2) {
    const U64 prev = td.repetition_table[td.repetition_index - (i - 1)];
    const U64 move_key = orig ^ prev;
    int j;
    if ((j = cuckoo_h1(move_key), cuckoo_key[j] == move_key) ||
        (j = cuckoo_h2(move_key), cuckoo_key[j] == move_key)) {
      const int s1 = cuckoo_move[j] & 63, s2 = (cuckoo_move[j] >> 6) & 63;
      if (between_tbl[s1][s2] & td.occupancies[both])
        continue; // percorso bloccato
      if (td.ply > i)
        return true; // ciclo tutto dentro il search path
      // Ciclo che pesca dietro la radice: vale solo se la mossa e' del lato al
      // tratto E la vecchia posizione era gia' una ripetizione (regola SF).
      const int occ_sq = get_bit(td.occupancies[both], s1) ? s1 : s2;
      if (!get_bit(td.occupancies[td.side], occ_sq))
        continue;
      for (int k2 = i + 2; k2 <= end; k2 += 2)
        if (td.repetition_table[td.repetition_index - (k2 - 1)] == prev)
          return true;
    }
  }
  return false;
}

static inline int td_is_repetition(ThreadData &td) {
  // P2.2 FastRepScan (default ON): finestra min(fifty, plies_from_null) invece
  // dell'INTERA storia. fifty = bound ESATTO (una mossa irreversibile azzera
  // fifty e cambia il board per sempre -> nessuna ricorrenza oltre);
  // plies_from_null esclude i segmenti oltre una null move — risolve il bug del
  // vecchio tentativo finestra-fifty (2026-06-06): le null pushano entry senza
  // toccare fifty e sfasavano la finestra. Niente step-2: la chiave include il
  // side_key, le entry dell'altro lato non possono mai combaciare.
  if (g_fast_rep_scan) {
    int limit = td.fifty < td.plies_from_null ? td.fifty : td.plies_from_null;
    int lo = td.repetition_index - limit;
    if (lo < 0)
      lo = 0;
    for (int i = td.repetition_index - 1; i >= lo; i--)
      if (td.repetition_table[i] == td.hash_key)
        return 1;
    return 0;
  }
  // Legacy: loop completo (entry null incluse), versione di riferimento.
  for (int i = 0; i < td.repetition_index; i++) {
    if (td.repetition_table[i] == td.hash_key)
      return 1;
  }
  return 0;
}
