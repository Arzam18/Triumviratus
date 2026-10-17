// search/12_quiesce.inc -- Node tools (hash table in the units of the search, legality, check, stepping into a child
// and back) and the quiescence search (block 5 of 8.1, in our form since 17/10/2026).
// Part of threads.cpp: NOT a translation unit of its own.

// ============================================================================
// TABELLA HASH: lettura e scrittura nelle unita' e nelle convenzioni della ricerca
// La profondita' si salva spostata di 3: -1 = quiescenza, -2 = solo valutazione, -3 = vuota.
// ============================================================================
constexpr int HD_QUIESCE = -1;
constexpr int HD_EVAL    = -2;
constexpr int HD_SHIFT   = 3;
// La quiescenza scrive e legge profondita' 0 in hash (05/10/2026, audit del nucleo; BAKE HASHQSDEPTH 06/10, opzione
// ritirata la sera: +3,38 +- 2,58 su 20.162 partite). C'e' un livello in piu' fra "solo valutazione" (-2) e
// quiescenza: le entry di ProbCut a cut_depth + 1 = -1 restano sotto la quiescenza (non tagliano in quiesce) e a
// profondita' 4 il piccolo ProbCut accetta le entry di quiescenza. Lo schema storico usava HD_QUIESCE = -1.
static inline int hd_qs() { return 0; }

enum { BOUND_UP = hash_flag_alpha, BOUND_LOW = hash_flag_beta, BOUND_EXACT = hash_flag_exact, BOUND_NIL = hash_flag_none };

struct HashView {
  bool hit;
  bool pv;
  int  move;
  int  value;       // VAL_NONE se assente
  int  depth;
  int  bound;
  int  eval_raw;    // valutazione salvata (senza smorzamento dei 50 mosse), tt_eval_none se assente
};

// Il limite confermato dall'entry vale per il confronto richiesto? (lower = serve un limite inferiore)
static inline bool bound_covers(int bound, bool lower) {
  return bound == BOUND_EXACT || bound == (lower ? BOUND_LOW : BOUND_UP);
}

static inline int to_hash_value(int v, int ply) {
  if (!val_known(v)) return v;
  return val_won(v) ? v + ply : val_lost(v) ? v - ply : v;
}

static inline int from_hash_value(int v, int ply, int fifty) {
  if (!val_known(v)) return VAL_NONE;
  if (val_won(v)) {
    if (v >= MATE_FLOOR && mate_value - v > 100 - fifty) return TB_WIN_FLOOR - 1;
    if (TB_WIN_TOP - v > 100 - fifty) return TB_WIN_FLOOR - 1;
    return v - ply;
  }
  if (val_lost(v)) {
    if (v <= -MATE_FLOOR && mate_value + v > 100 - fifty) return -TB_WIN_FLOOR + 1;
    if (TB_WIN_TOP + v > 100 - fifty) return -TB_WIN_FLOOR + 1;
    return v + ply;
  }
  return v;
}

// Lettura e scrittura passano da tt_key (03_tables.inc): la chiave della scacchiera piu', se accesa, la fascia del
// contatore delle 50 mosse (HashFiftyStep).
static inline void hash_read(const ThreadData &td, HashView &h) {
  int move, value, depth, flag, ev;
  bool pv;
  const bool usable = probe_tt(tt_key(td), move, value, depth, flag, ev, pv);
  h.hit = usable || flag == BOUND_NIL;
  if (!h.hit) {
    h.pv = false; h.move = 0; h.value = VAL_NONE; h.depth = -HD_SHIFT; h.bound = BOUND_NIL; h.eval_raw = tt_eval_none;
    return;
  }
  h.pv = pv;
  h.move = move;
  h.depth = depth - HD_SHIFT;
  h.bound = flag;
  h.value = flag == BOUND_NIL ? VAL_NONE : from_hash_value(value, td.ply, td.fifty);
  h.eval_raw = ev;
}

static inline void hash_write(const ThreadData &td, int value, bool pv, int bound, int depth, int move, int eval_raw) {
  store_tt(tt_key(td), move, to_hash_value(value, td.ply), depth + HD_SHIFT, bound, 0, pv, eval_raw);
}

// Toglie un ply all'entry: un limite che non ha tagliato a profondita' sufficiente vale meno.
static inline void hash_soften(U64 key) {
  tt_entry *e = tt_find(key);
  if (!e) return;
  const U64 d = e->data;
  const int dep = unpack_depth(d);
  if (dep == 0) return;
  const U64 nd = (d & ~(0xFFULL << 40)) | ((U64)(dep - 1) << 40);
  const U64 w = e->kw ^ d;
  e->data = nd;
  e->kw = w ^ nd;
}

// Valutazione statica del nodo: dalla hash se c'e', altrimenti dalla rete. raw = formato da salvare in hash.
static inline int node_eval(ThreadData &td, const HashView &h, int &raw) {
  if (h.hit && h.eval_raw != tt_eval_none) {
    raw = h.eval_raw;
    return eval_refade(h.eval_raw, td.fifty);
  }
  const int v = static_evaluate(td);
  raw = eval_unfade(v, td.fifty);
  return v;
}

// Idea 3 (Contempt, 04/10/2026): la patta vale -c per il colore al tratto alla radice e +c per l'avversario.
// Punta all'Elo di lista (CCRL: ~90% di patte), non al self-play. 0 = spento, valori di patta di prima.
static inline int contempt(const ThreadData &td) {
  // DrawDefend (13/10/2026): in svantaggio la patta vale +draw_defend per il colore di radice (vedi 01_params.inc).
  const int c = Contempt - (DrawDefend ? td.draw_defend : 0);
  return c ? (td.side == td.root_side ? -c : c) : 0;
}

// Patta a valore quasi nullo: +-1 secondo i nodi, per non trattare tutte le patte come identiche.
static inline int draw_score(const ThreadData &td) { return -1 + (int)(td.nodes & 2) + contempt(td); }

// ============================================================================
// LEGALITA' E SCACCO PRIMA DI MUOVERE
// ============================================================================

// a, b, c sulla stessa linea (riga, colonna o diagonale)?
static inline bool on_line(int a, int b, int c) {
  return (between_tbl[a][b] >> c & 1) || (between_tbl[a][c] >> b & 1) || (between_tbl[b][c] >> a & 1);
}

static inline U64 attackers_with(const ThreadData &td, int sq, int by, U64 occ) {
  const int off = by == white ? 0 : 6;
  const SliderPair sp = get_slider_pair(sq, occ);   // AA1 (10/10/2026): un calcolo per i due (magic.h)
  return (pawn_attacks[by ^ 1][sq] & td.bitboards[P + off]) | (knight_attacks[sq] & td.bitboards[N + off]) |
         (king_attacks[sq] & td.bitboards[K + off]) |
         (pair_bishop(sp) & (td.bitboards[B + off] | td.bitboards[Q + off])) |
         (pair_rook(sp) & (td.bitboards[R + off] | td.bitboards[Q + off]));
}

// Contesto del nodo per legalita' (inchiodature del nostro re) e scacco (case di scacco sul re nemico),
// calcolato una volta alla prima mossa.
struct MoveSafety {
  bool ready;
  int  our_king, their_king;
  U64  pinned;
  LmpChkCtx checks;
  bool pins_ready;   // GL: le inchiodature si calcolano solo se legal_before serve (mossa hash, Chess960)
};

// Inchiodature del nostro re, per legal_before.
static void safety_pins(ThreadData &td, MoveSafety &s) {
  const int us = td.side, eoff = us == white ? 6 : 0;
  const U64 occ = td.occupancies[both];
  U64 snipers = (empty_rook_att[s.our_king] & (td.bitboards[R + eoff] | td.bitboards[Q + eoff])) |
                (empty_bishop_att[s.our_king] & (td.bitboards[B + eoff] | td.bitboards[Q + eoff]));
  s.pinned = 0;
  while (snipers) {
    const int sn = get_ls1b_index(snipers);
    const U64 between = between_tbl[s.our_king][sn] & occ;
    if (between && !(between & (between - 1)) && (between & td.occupancies[us]))
      s.pinned |= between;
    pop_lsb_bb(snipers);
  }
  s.pins_ready = true;
}

static void safety_init(ThreadData &td, MoveSafety &s) {
  const int us = td.side, them = us ^ 1;
  const int off = us == white ? 0 : 6, eoff = them == white ? 0 : 6;
  s.our_king = get_ls1b_index(td.bitboards[K + off]);
  s.their_king = get_ls1b_index(td.bitboards[K + eoff]);
  // GL: con le liste legali legal_before serve solo alla mossa hash, che le chiede da se' (legal_before).
  s.pins_ready = false;
  if (!td_lists_legal())
    safety_pins(td, s);
  if (!s.checks.ready)   // l'ordinamento delle quiete puo' averle gia' calcolate
    td_lmp_chk_init(td, s.checks);
  s.ready = true;
}

// 06/10/2026 (velocita', stessa logica): legal_before chiamata ~2,2 volte per nodo (contatori, bench 14 su fens30) e,
// nel binario PGO, fuori linea con il prologo completo (otto registri salvati e ripresi) anche per il caso comune:
// pezzo non re, niente arrocco ne' e.p., di solito non inchiodato, ~10 istruzioni di lavoro. Ora il caso comune e'
// inline e i tre casi rari (arrocco, en passant, mosse di re) stanno in legal_before_slow, fuori linea.
__attribute__((noinline)) static bool legal_before_slow(ThreadData &td, MoveSafety &s, int m) {
  const int from = move_from(m), to = move_to(m), us = td.side, them = us ^ 1;
  if (get_move_castling(m))
    return g_chess960 || !td_is_square_attacked(td, to, them);
  if (get_move_enpassant(m)) {
    const int capsq = us == white ? to + 8 : to - 8;
    const U64 occ = (td.occupancies[both] ^ (1ULL << from) ^ (1ULL << capsq)) | (1ULL << to);
    const int eoff = them == white ? 0 : 6;
    const SliderPair sp = get_slider_pair(s.our_king, occ);
    return !(pair_rook(sp) & (td.bitboards[R + eoff] | td.bitboards[Q + eoff])) &&
           !(pair_bishop(sp) & (td.bitboards[B + eoff] | td.bitboards[Q + eoff]));
  }
  if (from == s.our_king)
    return !attackers_with(td, to, them, td.occupancies[both] ^ (1ULL << from));
  return !(s.pinned >> from & 1) || on_line(from, to, s.our_king);
}

static inline bool legal_before(ThreadData &td, MoveSafety &s, int m) {
  PROF_GUARD(prof_gc);
  if (!s.ready) safety_init(td, s);
  if (!s.pins_ready) safety_pins(td, s);
  const int from = move_from(m);
  if ((m & 0xC00000) || from == s.our_king)   // arrocco (bit 23), en passant (bit 22), re
    return legal_before_slow(td, s, m);
  return !(s.pinned >> from & 1) || on_line(from, move_to(m), s.our_king);
}

static bool checks_before(ThreadData &td, MoveSafety &s, int m) {
  PROF_GUARD(prof_gc);
  if (!s.ready) safety_init(td, s);
  const int from = move_from(m), to = move_to(m), pc = move_piece(m), pt = pc - (td.side == white ? 0 : 6);
  const bool castling = get_move_castling(m) != 0;
  if (!castling && (s.checks.chk[pt] >> to & 1))
    return true;
  if ((s.checks.disc >> from & 1) && (castling || !on_line(from, to, s.their_king)))
    return true;
  const U64 occ = td.occupancies[both];
  const int off = td.side == white ? 0 : 6;
  if (get_move_promoted(m)) {
    const U64 o = occ ^ (1ULL << from);
    U64 att = 0;
    switch (get_move_promoted(m) % 6) {
    case N: att = knight_attacks[to]; break;
    case B: att = get_bishop_attacks(to, o); break;
    case R: att = get_rook_attacks(to, o); break;
    default: att = get_queen_attacks(to, o); break;
    }
    return att >> s.their_king & 1;
  }
  if (get_move_enpassant(m)) {
    const int capsq = td.side == white ? to + 8 : to - 8;
    const U64 o = (occ ^ (1ULL << from) ^ (1ULL << capsq)) | (1ULL << to);
    const SliderPair sp = get_slider_pair(s.their_king, o);
    return (pair_rook(sp) & (td.bitboards[R + off] | td.bitboards[Q + off])) ||
           (pair_bishop(sp) & (td.bitboards[B + off] | td.bitboards[Q + off]));
  }
  if (castling) {
    int rook_pc, rf, rt;
    td_castle_rook(to, rook_pc, rf, rt);
    return s.checks.chk[R] >> rt & 1;
  }
  return false;
}

// ============================================================================
// PASSI: entrare in un figlio e tornare indietro
// ============================================================================
// legal (GL, 15/10/2026): la mossa viene da una lista del generatore legale (queue_move_legal), quindi la make salta il
// test del re anche sotto scacco.
static inline bool step_in(ThreadData &td, NodeFrame *f, int m, UndoInfo &u, bool legal = false) {
  td.repetition_table[++td.repetition_index] = td.hash_key;
  td.ply++;
  // ⛔ 04/10/2026 — TOLTA la "legalita' decisa una volta" (make con known_legal = true quando legal_before aveva
  // deciso): nessun guadagno misurabile, e legal_before NON e' completo sotto scacco. Vale per una mossa pseudo-legale
  // a re non sotto scacco o per una parata del generatore delle parate; la mossa hash, le killer e la contromossa
  // passano solo dalla pseudo-legalita', e una di loro che non parava lo scacco veniva giocata. Il re si catturava al
  // ply dopo e la posizione della rete restava senza re: crash in release (1 motore su 16 nel training PGO, 3 partite
  // perse per disconnessione su 197 contro SF19). La verifica con -DTRIUMV_VERIFY_LEGAL su 3,9 milioni di nodi non
  // l'aveva trovato.
  // ✅ 04/10/2026 sera — RIPRESA SOLO DOVE E' COMPLETA: a re NON sotto scacco legal_before decide da solo (inchiodature,
  // re che non entra in presa, en passant, arrocco) e la make salta il test; sotto scacco il test resta. Arrocco del
  // 960 sempre alla make. Con -DTRIUMV_VERIFY_LEGAL la make rifa' sempre il test e stampa ogni disaccordo.
#ifdef TRIUMV_VERIFY_LEGAL
  if ((!f->in_check || legal) && !td_make_move(td, m, u)) {
    printf("info string LEGALITA' SBAGLIATA: mossa %d ritenuta legale\n", m);
    fflush(stdout);
    td.ply--;
    td.repetition_index--;
    return false;
  }
  if (f->in_check && !legal && !td_make_move(td, m, u)) {
#else
  if (!td_make_move(td, m, u, (legal || !f->in_check) && !(g_chess960 && get_move_castling(m)))) {
#endif
    td.ply--;
    td.repetition_index--;
    return false;
  }
  td.nodes++;
  f->move = m;
  f->hctx = 0;   // ThreatHist: il ciclo delle mosse della ricerca lo riscrive per le sue quiete
  f->captured = u.captured_piece;
  f->cont_hist = &g_stats.follow[f->in_check][is_tactical(m)][move_piece(m)][move_to(m)];
  f->cont_corr = &td.cont_corr[move_piece(m)][move_to(m)];
  // CCPF (14/10/2026, velocita' ad albero identico): prefetch delle celle della correzione di continuazione che il
  // figlio legge per prime in corr_signal. Trovato da noi dal costo di CorrContOppW diverso fra i socket; poi visto che
  // Stockfish lo fa dall'agosto 2026 ("Prefetch continuation correction histories", 4150d22b86, gia' in SF19: la
  // riscrittura del 04/10 lo aveva perso). xperf v14 a macchina ferma: ~-0,3% cicli (SF: neutro su AVX-512, +0,5% Zen 3). Nel figlio f[-1] e' questo frame, quindi le righe sono quelle di f[-1] e f[-3]
  // qui (CorrContW, il figlio a -2 e -4) e di f[-2] (CorrContOppW, il figlio a -3), la cella e' (pezzo arrivato, casa).
  // Le correzioni per struttura sono gia' anticipate nella make; queste no. Misura del 14/10: con CorrContOppW accesa
  // il lato in prova perdeva -0,04 ply sul socket 1 (2 banchi di memoria) contro -0,01 sul socket 0: una lettura in
  // piu' per nodo che manca la cache sotto carico. Le cornici prima della radice esistono sempre (riga sentinella).
  {
    const int to = move_to(m), pc = td.piece_on[to];
    TT_PREFETCH(&(*f[-1].cont_corr)[pc][to]);
    TT_PREFETCH(&(*f[-3].cont_corr)[pc][to]);
    if (CorrContOppW)
      TT_PREFETCH(&(*f[-2].cont_corr)[pc][to]);
  }
  return true;
}

static inline void step_out(ThreadData &td, int m, UndoInfo &u) {
  td_unmake_move(td, m, u);
  td.ply--;
  td.repetition_index--;
}

struct NullUndo {
  int enpassant, fifty, plies_from_null;
  U64 hash;
};

static inline void step_in_null(ThreadData &td, NodeFrame *f, NullUndo &u) {
  td.repetition_table[++td.repetition_index] = td.hash_key;
  td.ply++;
  u.enpassant = td.enpassant;
  u.fifty = td.fifty;
  u.plies_from_null = td.plies_from_null;
  u.hash = td.hash_key;
  if (td.enpassant != no_sq)
    td.hash_key ^= enpassant_keys[td.enpassant];
  td.enpassant = no_sq;
  td.hash_key ^= side_key;
  td.side ^= 1;
  // La mossa nulla non incrementa il contatore delle 50 mosse (05/10/2026, audit del nucleo; BAKE PULIZIA 06/10,
  // opzione NullFiftyKeep ritirata la sera).
  td.plies_from_null = 0;
  // La rete non registra nulla: i pezzi non si muovono, e gli accumulatori non dipendono dal lato al tratto.
  f->move = NULL_MOVE;
  f->hctx = 0;
  f->captured = -1;
  f->cont_hist = &g_stats.follow[0][0][NO_PC][0];
  f->cont_corr = &td.cont_corr[NO_PC][0];
}

static inline void step_out_null(ThreadData &td, NullUndo &u) {
  td.side ^= 1;
  td.enpassant = u.enpassant;
  td.fifty = u.fifty;
  td.plies_from_null = u.plies_from_null;
  td.hash_key = u.hash;
  td.ply--;
  td.repetition_index--;
}

// Patta per la regola delle 50 mosse (salvo matto) o per ripetizione.
// 06/10/2026 (velocita', stessa logica): drawn_here gira a ogni nodo (~1,1 volte per nodo) e nel binario PGO e' fuori
// linea con un frame da 1,1 KB (la lista di mosse del caso "50 mosse sotto scacco"). In oltre l'80% delle chiamate la
// finestra reversibile e' sotto i 4 ply e repeated_draw non fa nemmeno un giro: quel caso ora esce inline.
__attribute__((noinline)) static bool drawn_here_slow(ThreadData &td, bool in_check) {
  PROF_GUARD(prof_rep);
  if (td.fifty > 99) {
    if (!in_check)
      return true;
    moves ml[1];
    td_generate_moves(td, ml, false, false, 1);
    if (td_lists_legal() && ml->count)   // GL: lista legale, una mossa basta
      return true;
    for (int i = 0; i < ml->count; i++) {
      UndoInfo u;
      td.repetition_table[++td.repetition_index] = td.hash_key;
      td.ply++;
      const bool ok = td_make_move(td, ml->moves[i], u) != 0;
      if (ok)
        td_unmake_move(td, ml->moves[i], u);
      td.ply--;
      td.repetition_index--;
      if (ok)
        return true;
    }
  }
  return repeated_draw(td);
}

static inline bool drawn_here(ThreadData &td, bool in_check) {
  if (td.fifty <= 99) {   // stessa finestra di repeated_draw: con meno di 4 ply non c'e' ripetizione possibile
    int end = td.fifty < td.plies_from_null ? td.fifty : td.plies_from_null;
    if (end > td.repetition_index)
      end = td.repetition_index;
    if (end < 4)
      return false;
  }
  return drawn_here_slow(td, in_check);
}

// InsufMaterial (10/10/2026; riscritta il 15/10/2026): nessuno dei due lati puo' forzare il matto. Solo re e pezzi
// minori: con al piu' un minore e' patta (KvK, KNvK, KBvK); con un minore per parte e' patta se nessun re e' in un
// angolo (con un re nell'angolo esistono matti, anche se non forzati); con due minori dalla stessa parte e' patta solo
// se sono due alfieri sullo stesso colore di case (KNNvK e KBBvK di colori diversi no, KBNvK vince). L'idea di
// chiudere cosi' il nodo viene da Reckless; la stessa regola e' in Stormphrax, Berserk, Viridithas, Caissa (solo dopo
// una cattura, come qui) e Coda. Il codice e' nostro: conta i minori per parte invece dei pezzi in totale. La rete da'
// a queste posizioni 3-15 cp (KNvK +11, KBvK +12): la regola non corregge una valutazione sbagliata ma chiude il nodo
// con una patta esatta.
static inline bool insufficient_material(const ThreadData &td) {
  if (td.bitboards[P] | td.bitboards[p] | td.bitboards[R] | td.bitboards[r] | td.bitboards[Q] | td.bitboards[q])
    return false;
  const U64 white_minors = td.bitboards[N] | td.bitboards[B], black_minors = td.bitboards[n] | td.bitboards[b];
  const int nw = count_bits(white_minors), nb = count_bits(black_minors);
  if (nw + nb <= 1)
    return true;
  if (nw + nb > 2)
    return false;
  if (nw == 1) {   // un minore per parte
    constexpr U64 corners = (1ULL << 0) | (1ULL << 7) | (1ULL << 56) | (1ULL << 63);
    return !((td.bitboards[K] | td.bitboards[k]) & corners);
  }
  const U64 pair = nw ? white_minors : black_minors;   // i due minori della stessa parte
  if (pair & (td.bitboards[N] | td.bitboards[n]))
    return false;
  constexpr U64 light = 0xAA55AA55AA55AA55ULL;   // un colore della scacchiera (a8 = 0): basta che sia una scacchiera
  return !(pair & light) || !(pair & ~light);
}

static bool has_legal_move(ThreadData &td) {
  moves ml[1];
  td_generate_moves(td, ml, false);
  if (td_lists_legal())   // GL: la lista e' gia' legale
    return ml->count != 0;
  for (int i = 0; i < ml->count; i++) {
    UndoInfo u;
    td.repetition_table[++td.repetition_index] = td.hash_key;
    td.ply++;
    const bool ok = td_make_move(td, ml->moves[i], u) != 0;
    if (ok)
      td_unmake_move(td, ml->moves[i], u);
    td.ply--;
    td.repetition_index--;
    if (ok)
      return true;
  }
  return false;
}

static inline void pv_extend(ThreadData &td, int m) {
  const int ply = td.ply;
  td.pv_table[ply][ply] = m;
  const int child_len = td.pv_length[ply + 1];
  for (int i = ply + 1; i < child_len; i++)
    td.pv_table[ply][i] = td.pv_table[ply + 1][i];
  td.pv_length[ply] = child_len > ply + 1 ? child_len : ply + 1;
}

// ============================================================================
// QUIESCENCE (block 5 in our form, 17/10/2026)
// A node of the quiescence search has four parts:
//   1. early exits: draws (repetition ahead, fifty moves, insufficient material) and the hash;
//   2. the stand: out of check, the corrected evaluation is a value the side to move can claim without moving;
//   3. the walk: tactical moves (all evasions when in check); qs_worth decides for each one whether it is searched,
//      and may raise the node's value with what a skipped move was certainly worth;
//   4. the close: mate or stalemate when nothing was played, the value pulled toward beta, the hash entry.
// Levers (01_params.inc): QsFut0-3, QsSee0-3, QsGate, QsVictimW, QsSeeFutW, QsFhMix, QsStandMix. At their
// defaults the tree is the one of the 8.0 quiescence, whose logic came from the algorithmic base of SF19.
// ============================================================================

// What the walk knows about the node.
struct QsWalk {
  int ex;         // expert of the node (piece count band of the network)
  int fut_base;   // stand + futility margin of the expert; -infinity in check (nothing is pruned on value)
  int prev_to;    // target square of the previous move: a recapture there is never pruned on value
};

// Pull a value that lies above beta toward beta by a mix weight (/1024); 1024 keeps the value.
static inline int qs_pull(int value, int beta, int mix) { return (mix * value + (1024 - mix) * beta) / 1024; }

// Is move m worth a search? 'seen' counts the legal moves met so far, m included. A skipped plain capture can still
// raise 'best' to the value it was sure to reach. While the side to move is being mated, every move is searched.
static inline bool qs_worth(ThreadData &td, const QsWalk &w, int m, bool gives, int seen, int alpha, int &best) {
  if (val_lost(best))
    return true;
  // A plain capture: no check, no promotion, not a recapture, and a futility base that is a real value.
  const bool plain = !gives && move_to(m) != w.prev_to && !get_move_promoted(m) && !val_lost(w.fut_base);
  if (plain) {
    if (seen > QsGate)
      return false;
    const int fut = w.fut_base + unit_value[victim_kind(td, m)] * QsVictimW / 1024;
    if (fut <= alpha) {   // even winning the victim for free leaves the node under alpha
      best = std::max(best, fut);
      return false;
    }
    if (!see_holds(td, m, (alpha - w.fut_base) * QsSeeFutW / 1024)) {   // the exchange cannot close the gap
      best = std::max(best, std::min(alpha, w.fut_base));
      return false;
    }
  }
  if (!is_tactical(m))   // quiet evasions are searched only while being mated (above)
    return false;
  return see_holds(td, m, -by_exp4(w.ex, QsSee0, QsSee1, QsSee2, QsSee3));
}

// QCHK (AnQsChecks, analysis mode, 09/10/2026, our idea): at the first quiescence ply called by the main search, quiet
// moves that give check with SEE >= 0 are tried after the tactical ones; the child (in check) is a normal quiescence.
// QCHK = false is the plain quiescence.
template <bool QCHK = false>
static int quiesce(ThreadData &td, NodeFrame *f, int alpha, int beta, bool pv) {
  // ---- 1. Early exits -------------------------------------------------------
  td.pv_length[td.ply] = td.ply;
  if (alpha < contempt(td) && td_upcoming_repetition(td)) {   // a repetition is in reach: the draw is a floor
    alpha = draw_score(td);
    if (alpha >= beta)
      return alpha;
  }
  if (pv && td.sel_depth < td.ply + 1)
    td.sel_depth = td.ply + 1;
  const bool in_check = f->in_check;
  if (td.ply >= max_ply)
    return in_check ? 0 : static_evaluate(td);
  if (drawn_here(td, in_check))
    return contempt(td);
  if (f[-1].captured >= 0 && insufficient_material(td))   // material changes only with a capture
    return contempt(td);

  HashView h;
  hash_read(td, h);
  f->tt_hit = h.hit;
  const bool hash_pv = h.hit && h.pv;
  if (!pv && h.depth >= hd_qs() && val_known(h.value) && bound_covers(h.bound, h.value >= beta))
    return h.value;

  // ---- 2. The stand ---------------------------------------------------------
  QsWalk w;
  w.ex = net_phase(count_bits(td.occupancies[both]));
  int best, eval_raw = tt_eval_none;
  if (in_check) {
    best = w.fut_base = -infinity;
  } else {
    const int signal = corr_signal(td, f);
    const int plain = node_eval(td, h, eval_raw);
    f->static_eval = best = corrected_eval(plain, signal + corr_signal_extra(td));
    // A hash bound on the right side is a better estimate than the evaluation.
    if (h.hit && val_known(h.value) && !val_decided(h.value) && bound_covers(h.bound, h.value > best))
      best = h.value;
    if (best >= beta) {   // standing still is already enough
      if (!val_decided(best))
        best = qs_pull(best, beta, QsFhMix);
      if (!h.hit)
        hash_write(td, VAL_NONE, false, BOUND_LOW, HD_EVAL, 0, eval_raw);
      return best;
    }
    alpha = std::max(alpha, best);
    w.fut_base = f->static_eval + by_exp4(w.ex, QsFut0, QsFut1, QsFut2, QsFut3);
  }

  // ---- 3. The walk ----------------------------------------------------------
  w.prev_to = move_ok(f[-1].move) ? move_to(f[-1].move) : -1;
  MoveQueue q;
  queue_start(q, td, f, h.move, 0, in_check ? QK_EVADE : QK_QUIESCE);
  MoveSafety safety;
  safety.ready = false;
  int best_move = 0, seen = 0, m;
  while ((m = queue_next(q)) != 0) {
    const bool gen_legal = queue_move_legal(q, m);
    if (!gen_legal && !legal_before(td, safety, m))
      continue;
    const bool gives = checks_before(td, safety, m);
    if (!qs_worth(td, w, m, gives, ++seen, alpha, best))
      continue;

    UndoInfo u;
    if (!step_in(td, f, m, u, gen_legal)) {   // illegal after all: it does not count as seen
      seen--;
      continue;
    }
    f[1].in_check = gives;
    const int value = -quiesce<false>(td, f + 1, -beta, -alpha, pv);
    step_out(td, m, u);

    if (value <= best)
      continue;
    best = value;
    if (value <= alpha)
      continue;
    best_move = m;
    if (pv)
      pv_extend(td, m);
    if (value >= beta)
      break;
    alpha = value;
  }
  int count = seen;

  if constexpr (QCHK) {
    if (!in_check && best < beta) {
      moves ml[1];
      td_generate_moves(td, ml, /*captures_only=*/false, /*quiets_only=*/true, /*known_in_check=*/0);
      const bool list_legal = td_lists_legal();
      for (int i = 0; i < ml->count && best < beta; i++) {
        const int qm = ml->moves[i];
        if (is_tactical(qm) || qm == q.hash_move || (!list_legal && !legal_before(td, safety, qm)) ||
            !checks_before(td, safety, qm) || !see_holds(td, qm, 0))
          continue;
        UndoInfo u;
        if (!step_in(td, f, qm, u, list_legal))
          continue;
        count++;
        f[1].in_check = true;
        const int value = -quiesce<false>(td, f + 1, -beta, -alpha, pv);
        step_out(td, qm, u);
        if (stop_threads.load(std::memory_order_relaxed))
          return 0;
        if (value > best) {
          best = value;
          if (value > alpha) {
            best_move = qm;
            if (pv)
              pv_extend(td, qm);
            if (value < beta)
              alpha = value;
          }
        }
      }
    }
  }

  // ---- 4. The close ---------------------------------------------------------
  if (!count) {
    if (in_check)
      return mated_at(td.ply);
    // Nothing was played and quiet moves are not generated here, so a stalemate can hide behind the stand. It is
    // looked for only when it is possible at all: besides the king, only blocked pawns, and the last move took a
    // piece (not a pawn).
    const int us = td.side, off = us == white ? 0 : 6;
    const U64 pawns = td.bitboards[P + off];
    const U64 pushes = us == white ? pawns >> 8 : pawns << 8;
    const U64 non_pawn = td.occupancies[us] & ~pawns & ~td.bitboards[K + off];
    const bool took_piece = f[-1].captured >= 0 && f[-1].captured % 6 >= N;
    if (!(pushes & ~td.occupancies[both]) && !non_pawn && took_piece && !has_legal_move(td))
      best = contempt(td);
  }
  if (!val_decided(best) && best > beta)
    best = qs_pull(best, beta, QsStandMix);
  hash_write(td, best, hash_pv, best >= beta ? BOUND_LOW : BOUND_UP, hd_qs(), best_move, eval_raw);
  return best;
}
