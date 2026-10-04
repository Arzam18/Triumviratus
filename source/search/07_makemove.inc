// search/07_makemove.inc -- Attacchi, make_move e unmake_move per thread.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// ATTACK DETECTION
// ============================================================================

static inline int td_is_square_attacked(ThreadData &td, int square,
                                        int attacker_side) {
  PROF_GUARD(prof_isatk);
#ifdef TRIUMV_PROFILE
  prof_n_isatk++;
#endif
  if (attacker_side == white) {
    if (pawn_attacks[black][square] & td.bitboards[P])
      return 1;
    if (knight_attacks[square] & td.bitboards[N])
      return 1;
    if (king_attacks[square] & td.bitboards[K])
      return 1;
    U64 occ = td.occupancies[both];
    if (get_bishop_attacks(square, occ) & (td.bitboards[B] | td.bitboards[Q]))
      return 1;
    if (get_rook_attacks(square, occ) & (td.bitboards[R] | td.bitboards[Q]))
      return 1;
  } else {
    if (pawn_attacks[white][square] & td.bitboards[p])
      return 1;
    if (knight_attacks[square] & td.bitboards[n])
      return 1;
    if (king_attacks[square] & td.bitboards[k])
      return 1;
    U64 occ = td.occupancies[both];
    if (get_bishop_attacks(square, occ) & (td.bitboards[b] | td.bitboards[q]))
      return 1;
    if (get_rook_attacks(square, occ) & (td.bitboards[r] | td.bitboards[q]))
      return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// LMPCheckGuard: "questa quiet da' scacco?" calcolato PRIMA della make.
// Il gives_check del motore (:4631+) vive DOPO la make; la LMP pota prima,
// quindi serve il test SF-style con le check-squares: le case da cui un pezzo
// di tipo T attaccherebbe il re nemico, calcolate UNA VOLTA per nodo
// (pigramente, solo se la LMP scatta davvero) e poi O(1) per mossa.
struct LmpChkCtx {
  bool ready = false;
  U64 chk[6] = {0, 0, 0,
                0, 0, 0}; // indicizzato per tipo: P,N,B,R,Q,K (K inutilizzato)
  U64 disc = 0;           // nostri pezzi che, muovendosi, scoprono uno scacco
};

static inline void td_lmp_chk_init(ThreadData &td, LmpChkCtx &c) {
  const int us = td.side;
  const int off = (us == white) ? 0 : 6;  // offset nostro nei bitboards
  const int eoff = (us == white) ? 6 : 0; // offset nemico
  const int eksq = get_ls1b_index(td.bitboards[K + eoff]);
  const U64 occ = td.occupancies[both];

  c.chk[N] = knight_attacks[eksq];
  c.chk[B] = get_bishop_attacks(eksq, occ);
  c.chk[R] = get_rook_attacks(eksq, occ);
  c.chk[Q] = c.chk[B] | c.chk[R];
  c.chk[P] =
      pawn_attacks[us ^ 1][eksq]; // da dove un NOSTRO pedone attacca quel re
  c.chk[K] = 0;

  // Scacchi di scoperta: un nostro pezzo e' l'unico schermo fra un nostro
  // slider e il re nemico. Il re nemico NON e' sotto scacco (e' il nostro
  // tratto), quindi qualunque nostro R/Q (risp. B/Q) che compare togliendo lo
  // schermo e' per forza dietro di esso.
  const U64 ours = td.occupancies[us];
  const U64 orq = td.bitboards[R + off] | td.bitboards[Q + off];
  const U64 obq = td.bitboards[B + off] | td.bitboards[Q + off];
  U64 cand = c.chk[R] & ours;
  while (cand) {
    int s = get_ls1b_index(cand);
    pop_bit(cand, s);
    if (get_rook_attacks(eksq, occ ^ (1ULL << s)) & orq)
      c.disc |= 1ULL << s;
  }
  cand = c.chk[B] & ours;
  while (cand) {
    int s = get_ls1b_index(cand);
    pop_bit(cand, s);
    if (get_bishop_attacks(eksq, occ ^ (1ULL << s)) & obq)
      c.disc |= 1ULL << s;
  }
  c.ready = true;
}

// ponytail: sovrastima di proposito. Un pezzo "disc" che si muove RESTANDO
// sulla linea del re non scopre nulla, ma qui conta comunque come scacco (manca
// una tabella aligned()) — e lo stesso per l'arrocco, dove la torre puo' dare
// scacco dalla casa d'arrivo. Sovrastimare = potare MENO = direzione sicura per
// un guard. Se il patch rende, si affina.
static inline bool td_lmp_gives_check(ThreadData &td, int move, LmpChkCtx &c) {
  PROF_GUARD(prof_gc);
  if (!c.ready)
    td_lmp_chk_init(td, c);
  const int tgt = get_move_target(move);
  const int pt = get_move_piece(move) % 6; // P..K indipendente dal colore
  return (c.chk[pt] & (1ULL << tgt)) ||
         (c.disc & (1ULL << get_move_source(move)));
}

// Casa di partenza/arrivo della TORRE nell'arrocco, dedotte dalla casa d'arrivo del re. Unica fonte per
// tutti i siti (occupazioni, chiavi, mailbox, make, rollback, unmake, NNUE). La partenza viene da
// castle_rook_sq (chess960.h): h1/a1/h8/a8 negli scacchi normali, la casa letta dalla FEN nel 960.
static inline void td_castle_rook(int target, int &rook_pc, int &rf, int &rt) {
  const int idx = castle_index(target);
  rook_pc = idx < 2 ? R : r;
  rf = castle_rook_sq[idx];
  rt = castle_rook_to(idx);
}

// Incremental occupancy update. Flips only the bits that the move touches,
// instead of OR-recomputing all 12 bitboards (+2.89% NPS, node-identical). XOR
// is self-inverse, so calling it again with the same move reverses it (used on
// make-forward, illegal rollback, and unmake). 'us' must be the MOVING side at
// the call site.
// captured_piece == -1 means no capture. For castling the rook squares derive
// from the king target. Produces occupancies identical to the full recompute.
static inline void td_occ_update(ThreadData &td, int us, int source, int target,
                                 int captured_square, int captured_piece,
                                 int castling) {
  // XOR fra le due case (non OR): uguale per ogni mossa normale (source != target), e nel Chess960 il re che
  // arrocca senza muoversi (source == target) non cambia nulla, invece di svuotare la propria casa. Con le
  // quattro case di re e torre in XOR anche le sovrapposizioni del 960 (re dove partiva la torre) tornano giuste.
  td.occupancies[us] ^= (1ULL << source) ^ (1ULL << target);
  if (captured_piece != -1)
    td.occupancies[us ^ 1] ^=
        (1ULL << captured_square); // capture (== target, or e.p. square)
  if (castling) {
    int rook_pc, rf, rt;
    td_castle_rook(target, rook_pc, rf, rt);
    td.occupancies[us] ^= (1ULL << rf) ^ (1ULL << rt);
  }
  td.occupancies[both] = td.occupancies[white] | td.occupancies[black];
}

#ifdef TRIUMV_MAILBOX_VERIFY
// Verifica esaustiva mailbox<->bitboard, per la validazione. Il perft del motore usa
// make_move GLOBALE e non passa mai di qui, quindi senza questo controllo non ci
// sarebbe modo di provare che i rami arrocco/e.p./promozione siano coperti: un
// disallineamento non da' crash ne' firma diversa, solo un ordinamento che sbaglia.
//   build:  -DTRIUMV_MAILBOX_VERIFY   poi   echo bench | motore
static inline void td_mailbox_verify(ThreadData &td, const char *where) {
  for (int sq = 0; sq < 64; sq++) {
    int expect = -1;
    for (int pc = P; pc <= k; pc++)
      if (get_bit(td.bitboards[pc], sq)) { expect = pc; break; }
    if (td.piece_on[sq] != expect) {
      fprintf(stderr, "[MAILBOX] %s: sq=%d mailbox=%d bitboards=%d\n", where, sq,
              td.piece_on[sq], expect);
      fflush(stderr);
      abort();
    }
  }
}
  #define MB_VERIFY(td, where) td_mailbox_verify((td), (where))
#else
  #define MB_VERIFY(td, where) ((void) 0)
#endif

// MAILBOX — le due meta' NON sono self-inverse come td_occ_update (che e' uno XOR):
// qui si scrivono valori, quindi servono andata e ritorno distinti. Vanno chiamate
// negli STESSI TRE SITI di td_occ_update, o la mailbox si disallinea dai bitboard
// e l'ordinamento sbaglia in silenzio (nessun crash, nessuna firma diversa).
static inline void td_mailbox_apply(ThreadData &td, int piece, int source, int target,
                                    int promoted, int captured_piece,
                                    int captured_square, int castling) {
  // La vittima PRIMA del pezzo che arriva: nell'e.p. `captured_square` != target,
  // in una cattura normale coincidono e deve vincere il pezzo che arriva.
  if (castling) {
    // Prima le due partenze, poi i due arrivi: nel Chess960 il re puo' arrivare dove partiva la torre (o
    // non muoversi), e scrivere "re arrivato" prima di "torre partita" cancellerebbe il re.
    int rook_pc, rf, rt;
    td_castle_rook(target, rook_pc, rf, rt);
    td.piece_on[source] = -1;
    td.piece_on[rf] = -1;
    td.piece_on[target] = piece;
    td.piece_on[rt] = rook_pc;
    return;
  }
  if (captured_piece != -1)
    td.piece_on[captured_square] = -1;
  td.piece_on[source] = -1;
  td.piece_on[target] = promoted ? promoted : piece;
}

static inline void td_mailbox_revert(ThreadData &td, int piece, int source, int target,
                                     int promoted, int captured_piece,
                                     int captured_square, int castling) {
  (void) promoted;  // in uscita il pezzo torna quello di partenza, promosso o no
  if (castling) {
    // Speculare a td_mailbox_apply: prima si svuotano i due arrivi, poi si rimettono re e torre.
    int rook_pc, rf, rt;
    td_castle_rook(target, rook_pc, rf, rt);
    td.piece_on[target] = -1;
    td.piece_on[rt] = -1;
    td.piece_on[source] = piece;
    td.piece_on[rf] = rook_pc;
    return;
  }
  // Ordine speculare: si svuota target e POI si rimette la vittima, cosi' quando
  // captured_square == target (cattura normale) la vittima riprende il suo posto.
  td.piece_on[target] = -1;
  td.piece_on[source] = piece;
  if (captured_piece != -1)
    td.piece_on[captured_square] = captured_piece;
}

// ============================================================================
// MAKE MOVE (returns 1 if legal)
// ============================================================================

// P2.1: aggiorna td.pawn_key (Zobrist solo-pedoni) via XOR. Self-inverse ->
// stessa chiamata (stessi argomenti) nei 3 siti di td_occ_update: make-forward,
// rollback della mossa illegale, unmake. Pedone mosso: via da source, atterra
// su target solo se NON promuove. Pedone catturato (anche e.p.): via da
// captured_square. Castling e null move non toccano pedoni. Consumata da
// td_corr_index (g_pawn_key_incr).
static inline void td_pawn_key_update(ThreadData &td, int piece, int source,
                                      int target, int promoted,
                                      int captured_piece, int captured_square) {
  if (piece == P || piece == p) {
    td.pawn_key ^= piece_keys[piece][source];
    if (!promoted)
      td.pawn_key ^= piece_keys[piece][target];
  }
  if (captured_piece == P || captured_piece == p)
    td.pawn_key ^= piece_keys[captured_piece][captured_square];
}

// CorrNonPawn: aggiorna td.np_key[colore] (Zobrist NON-pedoni per colore) via
// XOR. Self-inverse -> stessa chiamata nei 3 siti di td_pawn_key_update. Casi:
// mover non-pedone (via da source, atterra su target); PROMOZIONE (il pezzo
// promosso APPARE su target — il pedone che sparisce e' affare del pawn_key);
// cattura di non-pedone (via da captured_square, chiave del colore CATTURATO);
// ARROCCO (la torre si muove — caso assente nel pawn-key, i pedoni non
// arroccano).
static inline void td_np_key_update(ThreadData &td, int piece, int source,
                                    int target, int promoted,
                                    int captured_piece, int captured_square,
                                    int castling) {
  if (piece != P && piece != p) {
    const int c = (piece >= p) ? black : white;
    td.np_key[c] ^= piece_keys[piece][source];
    td.np_key[c] ^= piece_keys[piece][target];
  } else if (promoted) {
    const int c = (promoted >= p) ? black : white;
    td.np_key[c] ^= piece_keys[promoted][target];
  }
  if (captured_piece != -1 && captured_piece != P && captured_piece != p) {
    const int c = (captured_piece >= p) ? black : white;
    td.np_key[c] ^= piece_keys[captured_piece][captured_square];
  }
  if (castling) {
    int rook_pc, rf, rt;
    td_castle_rook(target, rook_pc, rf, rt);
    td.np_key[rook_pc == R ? white : black] ^= piece_keys[rook_pc][rf] ^ piece_keys[rook_pc][rt];
  }
}

// NPS 25/09/2026 — chiavi minori/maggiori INCREMENTALI (XOR self-inverse, stessi tre
// siti di td_np_key_update). Prima td_corr_index_minor/major riscandivano 8 bitboard
// fino a quattro volte per nodo (valore, incertezza, aggiornamento): il profilo xperf
// del 25/09 dava ~370 cicli/nodo al blocco "corr index/value", contro una manciata di
// letture in SF, che tiene minorPieceKey incrementale in do_move. Stessa chiave per
// costruzione => nodi identici. (Il 4/08 era stata bocciata una CACHE degli indici per
// hash_key, che e' un'altra cosa: aggiungeva un confronto e due campi senza togliere la
// prima scansione di ogni nodo.)
// Classe di un pezzo: 0 = minore (N/B), 1 = maggiore (R/Q), -1 = pedone o re.
static constexpr int kMmClass[12] = {-1, 0, 0, 1, 1, -1, -1, 0, 0, 1, 1, -1};
static inline void td_mm_key_update(ThreadData &td, int piece, int source,
                                    int target, int promoted,
                                    int captured_piece, int captured_square,
                                    int castling) {
  const int cm = kMmClass[piece];
  if (cm >= 0)
    td.mm_key[cm] ^= piece_keys[piece][source] ^ piece_keys[piece][target];
  if (promoted)
    td.mm_key[kMmClass[promoted]] ^= piece_keys[promoted][target];
  if (captured_piece != -1) {
    const int cc = kMmClass[captured_piece];
    if (cc >= 0)
      td.mm_key[cc] ^= piece_keys[captured_piece][captured_square];
  }
  if (castling) {
    int rook_pc, rf, rt;
    td_castle_rook(target, rook_pc, rf, rt);
    td.mm_key[1] ^= piece_keys[rook_pc][rf] ^ piece_keys[rook_pc][rt];
  }
}

// known_legal (04/10/2026): la ricerca decide la legalita' prima di muovere (legal_before, 12_quiesce.inc) e allora il
// test "il nostro re e' sotto attacco?" qui sotto e' lavoro doppio (1,06 test di attacco per nodo nel profilo).
// Gli altri chiamanti (perft, radice, validazioni) lo lasciano a false.
static inline int td_make_move(ThreadData &td, int move, UndoInfo &undo, bool known_legal = false) {
  PROF_GUARD(prof_make);
  undo.old_castle = td.castle;
  undo.old_enpassant = td.enpassant;
  undo.old_fifty = td.fifty;
  undo.old_hash = td.hash_key;
  undo.captured_piece = -1;
  undo.captured_square = -1;

  int source = get_move_source(move);
  int target = get_move_target(move);
  int piece = get_move_piece(move);
  int promoted = get_move_promoted(move);
  int capture = get_move_capture(move);
  int double_push = get_move_double(move);
  int enpass = get_move_enpassant(move);
  int castling = get_move_castling(move);

  // NPS 25/09/2026 — PREFETCH ANTICIPATO (SF key_after). La chiave del figlio si stima
  // PRIMA di fare la mossa: pezzo da/a, vittima (dalla mailbox, ancora pre-mossa), lato,
  // e.p. azzerato. Ignora arrocco, promozione, nuova casa e.p. e diritti d'arrocco: in quei
  // casi (rari) la stima sbaglia e il prefetch si rifa' in fondo con la chiave vera. Il
  // profilo del 25/09 dava ~6% del tempo al carico dell'entry TT nel probe del figlio: il
  // prefetch partiva in fondo alla make e aveva poco anticipo. Nella stessa riga di cache
  // della eval cache del figlio (tabella da 1 MB per thread) parte il secondo prefetch.
  // Nodi identici: i prefetch non cambiano nulla di funzionale.
  U64 pf_key = td.hash_key ^ piece_keys[piece][source] ^ piece_keys[piece][target] ^ side_key;
  if (capture && !enpass) {
    const int vic = td.piece_on[target];
    if (vic >= 0)
      pf_key ^= piece_keys[vic][target];
  }
  if (td.enpassant != no_sq)
    pf_key ^= enpassant_keys[td.enpassant];
  TT_PREFETCH(&hash_table[tt_base_index(pf_key)]);
  TT_PREFETCH(&td.eval_cache[pf_key & ThreadData::EVAL_CACHE_MASK]);

  pop_bit(td.bitboards[piece], source);
  set_bit(td.bitboards[piece], target);
  td.hash_key ^= piece_keys[piece][source];
  td.hash_key ^= piece_keys[piece][target];

  td.fifty++;
  if (piece == P || piece == p)
    td.fifty = 0;

  if (capture) {
    td.fifty = 0;

    if (enpass) {
      undo.captured_square = (td.side == white) ? target + 8 : target - 8;
      undo.captured_piece = (td.side == white) ? p : P;
    } else {
      undo.captured_square = target;
      int start = (td.side == white) ? p : P;
      int end = (td.side == white) ? k : K;
      // La mailbox e' ancora quella PRE-mossa qui: si aggiorna piu' sotto, insieme
      // a td_occ_update. Il test di range replica esattamente il ciclo sostituito,
      // che cercava solo fra i pezzi AVVERSARI e lasciava -1 se non trovava nulla.
      const int mb_pc = td.piece_on[target];
      if (mb_pc >= start && mb_pc <= end)
        undo.captured_piece = mb_pc;
    }

    if (undo.captured_piece != -1) {
      pop_bit(td.bitboards[undo.captured_piece], undo.captured_square);
      td.hash_key ^= piece_keys[undo.captured_piece][undo.captured_square];
    }
  }

  if (promoted) {
    pop_bit(td.bitboards[piece], target);
    td.hash_key ^= piece_keys[piece][target];
    set_bit(td.bitboards[promoted], target);
    td.hash_key ^= piece_keys[promoted][target];
  }

  if (td.enpassant != no_sq)
    td.hash_key ^= enpassant_keys[td.enpassant];
  td.enpassant = no_sq;

  if (double_push) {
    int ep_sq = (td.side == white) ? target + 8 : target - 8;
    // F-017 EPKeyFix (default ON): ep nel board/key SOLO se un pedone nemico
    // puo' catturarlo (pseudo-legale, come SF). Niente "phantom ep" -> le
    // trasposizioni a2a3+a3a4 vs a2a4 hanno la stessa chiave = piu' TT-hit.
    // Nessun effetto sul movegen: senza capturer la casella ep era comunque
    // inusabile.
    if ((pawn_attacks[td.side][ep_sq] &
                          td.bitboards[(td.side == white) ? p : P])) {
      td.enpassant = ep_sq;
      td.hash_key ^= enpassant_keys[ep_sq];
    }
  }

  if (castling) {
    // Re e torre in bitboard diverse: anche con le sovrapposizioni del 960 pop/set non interferiscono.
    int rook_pc, rf, rt;
    td_castle_rook(target, rook_pc, rf, rt);
    pop_bit(td.bitboards[rook_pc], rf);
    set_bit(td.bitboards[rook_pc], rt);
    td.hash_key ^= piece_keys[rook_pc][rf];
    td.hash_key ^= piece_keys[rook_pc][rt];
  }

  td.hash_key ^= castle_keys[td.castle];
  td.castle &= castling_rights[source];
  td.castle &= castling_rights[target];
  td.hash_key ^= castle_keys[td.castle];

  td_occ_update(td, td.side, source, target, undo.captured_square,
                undo.captured_piece, castling);
  td_pawn_key_update(td, piece, source, target, promoted, undo.captured_piece,
                     undo.captured_square);
  td_np_key_update(td, piece, source, target, promoted, undo.captured_piece,
                   undo.captured_square, castling);
  td_mm_key_update(td, piece, source, target, promoted, undo.captured_piece,
                   undo.captured_square, castling);
  td_mailbox_apply(td, piece, source, target, promoted, undo.captured_piece,
                   undo.captured_square, castling);  // sito 1/3: make-forward
  MB_VERIFY(td, "make-forward");

  td.side ^= 1;
  td.hash_key ^= side_key;

  if (!known_legal &&
      td_is_square_attacked(td, get_ls1b_index((td.side == white) ? td.bitboards[k] : td.bitboards[K]), td.side)) {
    td.side ^= 1;

    if (promoted) {
      pop_bit(td.bitboards[promoted], target);
      set_bit(td.bitboards[piece], source);
    } else {
      pop_bit(td.bitboards[piece], target);
      set_bit(td.bitboards[piece], source);
    }

    if (undo.captured_piece != -1) {
      set_bit(td.bitboards[undo.captured_piece], undo.captured_square);
    }

    if (castling) {
      int rook_pc, rf, rt;
      td_castle_rook(target, rook_pc, rf, rt);
      pop_bit(td.bitboards[rook_pc], rt);
      set_bit(td.bitboards[rook_pc], rf);
    }

    td.castle = undo.old_castle;
    td.enpassant = undo.old_enpassant;
    td.fifty = undo.old_fifty;
    td.hash_key = undo.old_hash;

    td_occ_update(td, td.side, source, target, undo.captured_square,
                  undo.captured_piece, castling);
    td_pawn_key_update(td, piece, source, target, promoted,
                       undo.captured_piece, undo.captured_square);
    td_np_key_update(td, piece, source, target, promoted, undo.captured_piece,
                     undo.captured_square, castling);
    td_mm_key_update(td, piece, source, target, promoted, undo.captured_piece,
                     undo.captured_square, castling);
    td_mailbox_revert(td, piece, source, target, promoted, undo.captured_piece,
                      undo.captured_square, castling);  // sito 2/3: mossa illegale
    MB_VERIFY(td, "rollback-illegale");

    return 0;
  }

  // Prefetch delle correzioni che il figlio legge per prime: struttura pedonale e pezzi minori (chiavi gia'
  // aggiornate qui sopra). td.side e' gia' quello del figlio. Dal 04/10/2026 le tabelle sono condivise fra i
  // thread e grandi (1 MB per thread), non piu' 64 KB in L2: il prefetch qui ha di nuovo dei miss da nascondere.
  TT_PREFETCH(&g_stats.corr[td.pawn_key & g_stats.corr_mask][td.side]);
  TT_PREFETCH(&g_stats.corr[td.mm_key[0] & g_stats.corr_mask][td.side]);

  if (td.hash_key != pf_key) { // stima sbagliata (arrocco, promozione, e.p.)
    TT_PREFETCH(&hash_table[tt_base_index(td.hash_key)]);
    TT_PREFETCH(&td.eval_cache[td.hash_key & ThreadData::EVAL_CACHE_MASK]);
  }

  // Mirror the (now legal) move on the incremental NNUE position, in
  // Stockfish encoding. The moving piece is dirtyPiece[0] (king-refresh).
  // (NPS 25/09/2026: provata la SfMove PIGRA, convertita solo nel catch-up. Misurata con i
  //  contatori hardware: +0,15% di istruzioni/nodo, nessun guadagno. Quasi ogni mossa
  //  fatta arriva a una valutazione, quindi rimandare la conversione non risparmia nulla.
  //  Rimossa.)
  {
    SfMove sm;
    sm.movedPiece = nn_piece_code[piece];
    sm.from = nnue_squares[source];
    sm.to = nnue_squares[target];
    sm.promoPiece = promoted ? nn_piece_code[promoted] : 0;
    sm.capturedPiece =
        (undo.captured_piece != -1) ? nn_piece_code[undo.captured_piece] : 0;
    sm.capturedSq =
        (undo.captured_piece != -1) ? nnue_squares[undo.captured_square] : -1;
    sm.rookPiece = 0;
    sm.rookFrom = -1;
    sm.rookTo = -1;
    if (castling) {
      int rpc, rf, rt;
      td_castle_rook(target, rpc, rf, rt);
      sm.rookPiece = nn_piece_code[rpc];
      sm.rookFrom = nnue_squares[rf];
      sm.rookTo = nnue_squares[rt];
    }
    sm.rule50 = td.fifty;
    nn_pos_do(td.nnpos, &sm);
  }

  td.plies_from_null++; // P2.2: una mossa reale in piu' dall'ultima null

  return 1;
}

// ============================================================================
// UNMAKE MOVE
// ============================================================================

static inline void td_unmake_move(ThreadData &td, int move, UndoInfo &undo) {
  PROF_GUARD(prof_make);
  nn_pos_undo(td.nnpos); // retract the move on the incremental NNUE mirror
  td.plies_from_null--;  // P2.2 (speculare al ++ del make riuscito)
  td.side ^= 1;

  int source = get_move_source(move);
  int target = get_move_target(move);
  int piece = get_move_piece(move);
  int promoted = get_move_promoted(move);
  int castling = get_move_castling(move);

  if (promoted) {
    pop_bit(td.bitboards[promoted], target);
    set_bit(td.bitboards[piece], source);
  } else {
    pop_bit(td.bitboards[piece], target);
    set_bit(td.bitboards[piece], source);
  }

  if (undo.captured_piece != -1) {
    set_bit(td.bitboards[undo.captured_piece], undo.captured_square);
  }

  if (castling) {
    int rook_pc, rf, rt;
    td_castle_rook(target, rook_pc, rf, rt);
    pop_bit(td.bitboards[rook_pc], rt);
    set_bit(td.bitboards[rook_pc], rf);
  }

  td.castle = undo.old_castle;
  td.enpassant = undo.old_enpassant;
  td.fifty = undo.old_fifty;
  td.hash_key = undo.old_hash;

  td_occ_update(td, td.side, source, target, undo.captured_square,
                undo.captured_piece, castling);
  td_pawn_key_update(td, piece, source, target, promoted, undo.captured_piece,
                     undo.captured_square);
  td_np_key_update(td, piece, source, target, promoted, undo.captured_piece,
                   undo.captured_square, castling);
  td_mm_key_update(td, piece, source, target, promoted, undo.captured_piece,
                   undo.captured_square, castling);
  td_mailbox_revert(td, piece, source, target, promoted, undo.captured_piece,
                    undo.captured_square, castling);  // sito 3/3: unmake
  MB_VERIFY(td, "unmake");
}
