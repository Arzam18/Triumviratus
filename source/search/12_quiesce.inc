// search/12_quiesce.inc -- Strumenti dei nodi (tabella hash, legalita', scacco, passi avanti e indietro) e
// ricerca di quiescenza.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

// ============================================================================
// TABELLA HASH: lettura e scrittura nelle unita' e nelle convenzioni della ricerca
// La profondita' si salva spostata di 3: -1 = quiescenza, -2 = solo valutazione, -3 = vuota.
// ============================================================================
constexpr int HD_QUIESCE = -1;
constexpr int HD_EVAL    = -2;
constexpr int HD_SHIFT   = 3;

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

static inline void hash_read(const ThreadData &td, HashView &h) {
  int move, value, depth, flag, ev;
  bool pv;
  const bool usable = probe_tt(td.hash_key, move, value, depth, flag, ev, pv);
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
  store_tt(td.hash_key, move, to_hash_value(value, td.ply), depth + HD_SHIFT, bound, 0, pv, eval_raw);
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

// Patta a valore quasi nullo: +-1 secondo i nodi, per non trattare tutte le patte come identiche.
static inline int draw_score(const ThreadData &td) { return -1 + (int)(td.nodes & 2); }

// ============================================================================
// LEGALITA' E SCACCO PRIMA DI MUOVERE
// ============================================================================

// a, b, c sulla stessa linea (riga, colonna o diagonale)?
static inline bool on_line(int a, int b, int c) {
  return (between_tbl[a][b] >> c & 1) || (between_tbl[a][c] >> b & 1) || (between_tbl[b][c] >> a & 1);
}

static inline U64 attackers_with(const ThreadData &td, int sq, int by, U64 occ) {
  const int off = by == white ? 0 : 6;
  return (pawn_attacks[by ^ 1][sq] & td.bitboards[P + off]) | (knight_attacks[sq] & td.bitboards[N + off]) |
         (king_attacks[sq] & td.bitboards[K + off]) |
         (get_bishop_attacks(sq, occ) & (td.bitboards[B + off] | td.bitboards[Q + off])) |
         (get_rook_attacks(sq, occ) & (td.bitboards[R + off] | td.bitboards[Q + off]));
}

// Contesto del nodo per legalita' (inchiodature del nostro re) e scacco (case di scacco sul re nemico),
// calcolato una volta alla prima mossa.
struct MoveSafety {
  bool ready;
  int  our_king, their_king;
  U64  pinned;
  LmpChkCtx checks;
};

static void safety_init(ThreadData &td, MoveSafety &s) {
  const int us = td.side, them = us ^ 1;
  const int off = us == white ? 0 : 6, eoff = them == white ? 0 : 6;
  s.our_king = get_ls1b_index(td.bitboards[K + off]);
  s.their_king = get_ls1b_index(td.bitboards[K + eoff]);
  const U64 occ = td.occupancies[both];
  U64 snipers = (get_rook_attacks(s.our_king, 0) & (td.bitboards[R + eoff] | td.bitboards[Q + eoff])) |
                (get_bishop_attacks(s.our_king, 0) & (td.bitboards[B + eoff] | td.bitboards[Q + eoff]));
  s.pinned = 0;
  while (snipers) {
    const int sn = get_ls1b_index(snipers);
    const U64 between = between_tbl[s.our_king][sn] & occ;
    if (between && !(between & (between - 1)) && (between & td.occupancies[us]))
      s.pinned |= between;
    pop_lsb_bb(snipers);
  }
  if (!s.checks.ready)   // l'ordinamento delle quiete puo' averle gia' calcolate
    td_lmp_chk_init(td, s.checks);
  s.ready = true;
}

static bool legal_before(ThreadData &td, MoveSafety &s, int m) {
  PROF_GUARD(prof_gc);
  if (!s.ready) safety_init(td, s);
  const int from = move_from(m), to = move_to(m), us = td.side, them = us ^ 1;
  if (get_move_castling(m))
    return g_chess960 || !td_is_square_attacked(td, to, them);
  if (get_move_enpassant(m)) {
    const int capsq = us == white ? to + 8 : to - 8;
    const U64 occ = (td.occupancies[both] ^ (1ULL << from) ^ (1ULL << capsq)) | (1ULL << to);
    const int eoff = them == white ? 0 : 6;
    return !(get_rook_attacks(s.our_king, occ) & (td.bitboards[R + eoff] | td.bitboards[Q + eoff])) &&
           !(get_bishop_attacks(s.our_king, occ) & (td.bitboards[B + eoff] | td.bitboards[Q + eoff]));
  }
  if (from == s.our_king)
    return !attackers_with(td, to, them, td.occupancies[both] ^ (1ULL << from));
  return !(s.pinned >> from & 1) || on_line(from, to, s.our_king);
}

static bool checks_before(ThreadData &td, MoveSafety &s, int m) {
  PROF_GUARD(prof_gc);
  if (!s.ready) safety_init(td, s);
  const int from = move_from(m), to = move_to(m), pc = move_piece(m), pt = pc % 6;
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
    return (get_rook_attacks(s.their_king, o) & (td.bitboards[R + off] | td.bitboards[Q + off])) ||
           (get_bishop_attacks(s.their_king, o) & (td.bitboards[B + off] | td.bitboards[Q + off]));
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
static inline bool step_in(ThreadData &td, NodeFrame *f, int m, UndoInfo &u) {
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
  if (!f->in_check && !td_make_move(td, m, u)) {
    printf("info string LEGALITA' SBAGLIATA: mossa %d ritenuta legale\n", m);
    fflush(stdout);
    td.ply--;
    td.repetition_index--;
    return false;
  }
  if (f->in_check && !td_make_move(td, m, u)) {
#else
  if (!td_make_move(td, m, u, !f->in_check && !(g_chess960 && get_move_castling(m)))) {
#endif
    td.ply--;
    td.repetition_index--;
    return false;
  }
  td.nodes++;
  f->move = m;
  f->captured = u.captured_piece;
  f->cont_hist = &g_stats.follow[f->in_check][is_tactical(m)][move_piece(m)][move_to(m)];
  f->cont_corr = &td.cont_corr[move_piece(m)][move_to(m)];
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
  td.fifty++;
  td.plies_from_null = 0;
  nn_pos_do_null(td.nnpos, td.fifty);
  f->move = NULL_MOVE;
  f->captured = -1;
  f->cont_hist = &g_stats.follow[0][0][NO_PC][0];
  f->cont_corr = &td.cont_corr[NO_PC][0];
}

static inline void step_out_null(ThreadData &td, NullUndo &u) {
  nn_pos_undo(td.nnpos);
  td.side ^= 1;
  td.enpassant = u.enpassant;
  td.fifty = u.fifty;
  td.plies_from_null = u.plies_from_null;
  td.hash_key = u.hash;
  td.ply--;
  td.repetition_index--;
}

// Patta per la regola delle 50 mosse (salvo matto) o per ripetizione.
static bool drawn_here(ThreadData &td, bool in_check) {
  PROF_GUARD(prof_rep);
  if (td.fifty > 99) {
    if (!in_check)
      return true;
    moves ml[1];
    td_generate_moves(td, ml, false, false, 1);
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

static bool has_legal_move(ThreadData &td) {
  moves ml[1];
  td_generate_moves(td, ml, false);
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
// QUIESCENZA
// ============================================================================
static int quiesce(ThreadData &td, NodeFrame *f, int alpha, int beta, bool pv) {
  td.pv_length[td.ply] = td.ply;
  if (alpha < 0 && td_upcoming_repetition(td)) {
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
    return 0;

  HashView h;
  hash_read(td, h);
  f->tt_hit = h.hit;
  const bool hash_pv = h.hit && h.pv;
  if (!pv && h.depth >= HD_QUIESCE && val_known(h.value) && bound_covers(h.bound, h.value >= beta))
    return h.value;

  int best, fut_base, eval_raw = tt_eval_none;
  if (in_check) {
    best = fut_base = -infinity;
  } else {
    const int signal = corr_signal(td, f);
    const int plain = node_eval(td, h, eval_raw);
    f->static_eval = best = corrected_eval(plain, signal);
    if (h.hit && val_known(h.value) && !val_decided(h.value) && bound_covers(h.bound, h.value > best))
      best = h.value;
    if (best >= beta) {
      if (!val_decided(best))
        best = (441 * best + 583 * beta) / 1024;
      if (!h.hit)
        hash_write(td, VAL_NONE, false, BOUND_LOW, HD_EVAL, 0, eval_raw);
      return best;
    }
    if (best > alpha)
      alpha = best;
    fut_base = f->static_eval + QsFutMargin;
  }

  const int prev = f[-1].move;
  const int prev_to = move_ok(prev) ? move_to(prev) : -1;
  MoveQueue q;
  queue_start(q, td, f, h.move, 0, in_check ? QK_EVADE : QK_QUIESCE);
  MoveSafety safety;
  safety.ready = false;
  int best_move = 0, count = 0, m;

  while ((m = queue_next(q)) != 0) {
    if (!legal_before(td, safety, m))
      continue;
    const bool gives = checks_before(td, safety, m);
    const bool tact = is_tactical(m);
    count++;

    if (!val_lost(best)) {
      if (!gives && move_to(m) != prev_to && !val_lost(fut_base) && !get_move_promoted(m)) {
        if (count > 2)
          continue;
        const int fut = fut_base + unit_value[victim_kind(td, m)];
        if (fut <= alpha) {
          best = std::max(best, fut);
          continue;
        }
        if (!see_holds(td, m, alpha - fut_base)) {
          best = std::max(best, std::min(alpha, fut_base));
          continue;
        }
      }
      if (!tact)
        continue;
      if (!see_holds(td, m, -QsSeeMargin))
        continue;
    }

    UndoInfo u;
    if (!step_in(td, f, m, u)) {
      count--;
      continue;
    }
    f[1].in_check = gives;
    const int value = -quiesce(td, f + 1, -beta, -alpha, pv);
    step_out(td, m, u);

    if (value > best) {
      best = value;
      if (value > alpha) {
        best_move = m;
        if (pv)
          pv_extend(td, m);
        if (value < beta)
          alpha = value;
        else
          break;
      }
    }
  }

  if (!count) {
    if (in_check)
      return mated_at(td.ply);
    // Nessuna cattura: se oltre al re restano solo pedoni bloccati e l'ultima mossa ha catturato un pezzo
    // (non un pedone), controlla lo stallo (la quiescenza non vede le mosse quiete).
    const int us = td.side, off = us == white ? 0 : 6;
    const U64 pawns = td.bitboards[P + off];
    const U64 pushes = us == white ? pawns >> 8 : pawns << 8;
    const U64 non_pawn = td.occupancies[us] & ~pawns & ~td.bitboards[K + off];
    const bool took_piece = f[-1].captured >= 0 && f[-1].captured % 6 >= N;
    if (!(pushes & ~td.occupancies[both]) && !non_pawn && took_piece && !has_legal_move(td))
      best = 0;
  }

  if (!val_decided(best) && best > beta)
    best = (462 * best + 562 * beta) / 1024;
  hash_write(td, best, hash_pv, best >= beta ? BOUND_LOW : BOUND_UP, HD_QUIESCE, best_move, eval_raw);
  return best;
}
