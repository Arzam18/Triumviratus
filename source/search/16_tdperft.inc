// search/15_tdperft.inc -- `tdperft N`: perft sulla scacchiera PER THREAD (quella della ricerca), con verifiche.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Solo build di sviluppo (#ifndef TRIUMV_FROZEN).
//
// Nato nell'audit D (26/09/2026, 171 posizioni, 482M nodi, 0 errori) e portato nel sorgente il 27/09 per il
// Chess960 (tools/perft960.py --cmd tdperft). Il perft classico usa make_move GLOBALE: questo invece passa da
// td_generate_moves / td_make_move / td_unmake_move, e a ogni nodo controlla:
//   - hash, pawn key, chiavi non-pedone e minori/maggiori ricalcolate da zero contro quelle incrementali;
//   - la mailbox contro le bitboard;
//   - td_is_pseudo_legal sulle mosse del PADRE (come una mossa TT o killer arrivata da un'altra posizione):
//     falso positivo = accetta una mossa che qui non si genera (plfp, deve essere 0).
#ifndef TRIUMV_FROZEN
static U64 tdp_key_mism = 0, tdp_pl_fp = 0, tdp_pl_fn = 0, tdp_nodes_checked = 0;
static void tdp_check_keys(ThreadData &td) {
  U64 h = 0, pk = 0, npw = 0, npb = 0, mm0 = 0, mm1 = 0;
  for (int pc = P; pc <= k; pc++) {
    U64 bb = td.bitboards[pc];
    while (bb) {
      int sq = get_ls1b_index(bb);
      h ^= piece_keys[pc][sq];
      if (pc == P || pc == p) pk ^= piece_keys[pc][sq];
      if (pc >= N && pc <= K) npw ^= piece_keys[pc][sq];
      if (pc >= n && pc <= k) npb ^= piece_keys[pc][sq];
      if (pc == N || pc == B || pc == n || pc == b) mm0 ^= piece_keys[pc][sq];
      if (pc == R || pc == Q || pc == r || pc == q) mm1 ^= piece_keys[pc][sq];
      if (td.piece_on[sq] != pc) {
        if (tdp_key_mism < 5) printf("info string MAILBOX MISMATCH sq=%d\n", sq);
        tdp_key_mism++;
      }
      pop_bit(bb, sq);
    }
  }
  if (td.enpassant != no_sq) h ^= enpassant_keys[td.enpassant];
  h ^= castle_keys[td.castle];
  if (td.side == black) h ^= side_key;
  tdp_nodes_checked++;
  const bool np_bad = (npw != td.np_key[white] || npb != td.np_key[black]);
  U64 occ_w = 0, occ_b = 0;
  for (int pc = P; pc <= K; pc++) occ_w |= td.bitboards[pc];
  for (int pc = p; pc <= k; pc++) occ_b |= td.bitboards[pc];
  const bool occ_bad = occ_w != td.occupancies[white] || occ_b != td.occupancies[black] ||
                       (occ_w | occ_b) != td.occupancies[both];
  if (h != td.hash_key || pk != td.pawn_key || np_bad || occ_bad ||
      mm0 != td.mm_key[0] || mm1 != td.mm_key[1]) {
    if (tdp_key_mism < 5)
      printf("info string KEY MISMATCH hash=%d pawn=%d np=%d mm=%d occ=%d\n", h != td.hash_key,
             pk != td.pawn_key, (npw != td.np_key[white]) || (npb != td.np_key[black]),
             (mm0 != td.mm_key[0]) || (mm1 != td.mm_key[1]), occ_bad);
    tdp_key_mism++;
  }
}
static U64 tdp_rec(ThreadData &td, int depth, const moves *parent) {
  moves ml[1];
  td_generate_moves(td, ml);
  if (parent) {  // mosse del padre (come killer/TT move da un'altra posizione): pseudo-legale solo se generata qui
    for (int i = 0; i < parent->count; i++) {
      const int m = parent->moves[i];
      bool in_list = false;
      for (int j = 0; j < ml->count; j++) if (ml->moves[j] == m) { in_list = true; break; }
      const bool pl = td_is_pseudo_legal(td, m);
      if (pl && !in_list) { if (tdp_pl_fp < 5) printf("info string PSEUDO-LEGAL FALSE POSITIVE move=%d\n", m); tdp_pl_fp++; }
      if (!pl && in_list) tdp_pl_fn++;
    }
  }
  if (depth == 0) return 1;
  U64 n = 0;
  for (int i = 0; i < ml->count; i++) {
    const int mv = ml->moves[i];
    UndoInfo u;
    td.ply++;
    td.repetition_table[++td.repetition_index] = td.hash_key;
    if (!td_make_move(td, mv, u)) { td.ply--; td.repetition_index--; tdp_check_keys(td); continue; }
    tdp_check_keys(td);
    n += tdp_rec(td, depth - 1, ml);
    td_unmake_move(td, mv, u);
    td.ply--;
    td.repetition_index--;
  }
  return n;
}
void td_perft_driver(int depth) {
  ThreadData &td = thread_data[0];
  copy_board_to_thread(td);
  nn_root_sync(td);
  tdp_key_mism = tdp_pl_fp = tdp_pl_fn = tdp_nodes_checked = 0;
  tdp_check_keys(td);
  // perft vero: conta le foglie a profondita' depth (tdp_rec conta le foglie quando depth==0)
  U64 leaves = 0;
  moves ml[1];
  td_generate_moves(td, ml);
  for (int i = 0; i < ml->count; i++) {
    const int mv = ml->moves[i];
    UndoInfo u;
    td.ply++;
    td.repetition_table[++td.repetition_index] = td.hash_key;
    if (!td_make_move(td, mv, u)) { td.ply--; td.repetition_index--; tdp_check_keys(td); continue; }
    tdp_check_keys(td);
    leaves += depth > 1 ? tdp_rec(td, depth - 1, ml) : 1;
    td_unmake_move(td, mv, u);
    td.ply--;
    td.repetition_index--;
  }
  tdp_check_keys(td);
  printf("tdperft %d Nodes: %llu keymism %llu plfp %llu plfn %llu checked %llu\n", depth,
         (unsigned long long)leaves, (unsigned long long)tdp_key_mism, (unsigned long long)tdp_pl_fp,
         (unsigned long long)tdp_pl_fn, (unsigned long long)tdp_nodes_checked);
  fflush(stdout);
}
#endif  // TRIUMV_FROZEN
