// search/03_tables.inc -- Search tables: reductions, statistics shared between the threads, preparation of the
// threads and of their board.
// Part of threads.cpp: it is NOT a translation unit of its own.

// ---------------------------------------------------------------------------
// Late reductions: red_log[i] = LmrLogMul/128 * ln(i), recomputed at every search (LmrLogMul is an option).
// ---------------------------------------------------------------------------
static int red_log[256];

// Reciprocal of the first-ply divisor of score_quiets, M = ceil(2^40 / (1024 + ply x LpDecay)), per expert and ply
// (20/10/2026, speed, same values): one 64-bit division per entry here, at the start of each search and at startup,
// instead of one in every node that scores its quiet moves.
static unsigned long long lp_recip_tbl[4][LOW_PLY_SIZE];
static void build_lp_recip() {
  const int decay[4] = {LpDecay0, LpDecay1, LpDecay2, LpDecay3};
  for (int e = 0; e < 4; e++)
    for (int ply = 0; ply < LOW_PLY_SIZE; ply++) {
      const unsigned long long den = 1024ULL + (unsigned long long)ply * decay[e];
      lp_recip_tbl[e][ply] = ((1ULL << 40) + den - 1) / den;
    }
}
static const bool lp_recip_at_startup = (build_lp_recip(), true);

static void build_reduction_table() {
  red_log[0] = 0;
  for (int i = 1; i < 256; i++)
    red_log[i] = int(LmrLogMul / 128.0 * std::log(double(i)));
  build_lp_recip();
}

// ---------------------------------------------------------------------------
// Hash table key (ours, 05/10/2026). With HashFiftyStep > 0 the key also carries the band of the
// fifty-move counter: one band every HashFiftyStep plies above HashFiftyFrom (up to 15 bands). A
// position reached 70 plies after the last capture is not the same position reached at 10: the bound
// stored in the hash was searched at another distance from the draw and the network evaluates it damped. Below From the
// key is the usual one: opening and middlegame lose no transpositions. The board key (td.hash_key)
// stays intact for repetitions and for the evaluation cache (which stores the eval without damping).
// At HashFiftyStep = 0 the function returns td.hash_key: identical tree, in the release the branch disappears.
// ---------------------------------------------------------------------------
static constexpr U64 fifty_hash_keys[16] = {
    0, 0x2B4AE6736F4FC7BEULL, 0xEAD209722A16E0CBULL, 0x89AE5F87DF9B4018ULL, 0x8B26E98280A48722ULL,
    0x08BD40C8D758D0F7ULL, 0xF64A5A01895149D9ULL, 0x445D6E85630EE7C1ULL, 0x91423FEEA7C93B58ULL,
    0xAD3FBFD5C23F9759ULL, 0xD8DD9840329B5C29ULL, 0x7CB97C9AE2AC9416ULL, 0x1934510935C788F3ULL,
    0xDC22B41127891257ULL, 0xA2FE2D5BA8CB11FBULL, 0x24A5B78779ED9899ULL};

static inline U64 tt_key(const ThreadData &td) {
  if (!HashFiftyStep || td.fifty <= HashFiftyFrom)
    return td.hash_key;
  return td.hash_key ^ fifty_hash_keys[std::min(1 + (td.fifty - HashFiftyFrom - 1) / HashFiftyStep, 15)];
}

// ---------------------------------------------------------------------------
// Statistics shared between the threads. The follow-up (continuation), pawn-structure and correction tables
// are one for the whole engine and grow with the number of threads: every thread brings its slice of space and
// all learn in the same table. Concurrent writes on int16 are harmless for a statistic.
// ---------------------------------------------------------------------------
struct CorrSlot {           // the corrections read with different keys but in the same array
  int16_t pawn, minor, np_white, np_black;
  // 06/10/2026: removed the `major` field of CorrMajorW (closed idea: -2.1 +- 20.7). Slot from 10 to 8 bytes: row of 16
  // bytes, four rows per cache line, table from 1.31 to 1.05 MB per thread (report N_CORREZIONI 4.6).
  // xperf measurement on a quiet machine (06/10/2026, PGO release, 6 rounds): cycles/node middlegame +0.00%, endgames +0.26%:
  // neutral in speed. Kept because it removes a dead field.
};

struct SharedStats {
  // follow[in_check][capture][piece][square] -> piece x square row of the next move. Piece 12 = sentinel.
  PieceToRow (*follow)[2][NO_PC + 1][64] = nullptr;
  PieceToRow* pawn_rows = nullptr;      // [pawn-structure index]
  CorrSlot  (*corr)[2] = nullptr;       // [key index][side to move]
  int16_t   (*corr_x)[2] = nullptr;     // CorrExpertW: pawns per expert, on top of the shared one [index][side]
  U64 pawn_mask = 0;
  U64 corr_mask = 0;
  int slots = 0;                        // power of two >= number of threads
#ifdef TRIUMV_C2
  // --- C2 ordering (agent ORDER) --- C2StructHistW: [pawn-king signature][piece][to], 4096 rows whatever the number
  // of threads (the key takes ~230 values over the bench), shared like the pawn rows (09_history.inc).
  PieceToRow* struct_rows = nullptr;
#endif
};
static SharedStats g_stats;

constexpr int PAWN_ROWS_PER_SLOT = 8192;
constexpr int CORR_ROWS_PER_SLOT = 65536;
#ifdef TRIUMV_C2
constexpr int C2_STRUCT_ROWS = 1 << 12;   // the maximum of C2StructHistBits
#endif

// V1 (08/10/2026, speed, identical tree): the shared tables on large pages, like TT and network. With one thread they are
// ~21 MB (follow 5.5 MB, pawn_rows 13.6 MB, corr 1 MB, corr_x 256 KB) read at random at every node (5 continuation
// rows per quiet move, 4 corrections): on 4 KB pages they are ~5,200 pages against ~1,500 STLB entries of
// this Xeon, on 2 MB pages 11. Profile of 05/10: td_prefetch_child 2.9% and corr_signal 2.0% of the cycles, both
// memory costs. Without the SeLockMemoryPrivilege privilege large_pages_alloc falls back to normal VirtualAlloc
// (Windows) or to aligned allocation + madvise (Linux). The memory comes back zeroed, then
// shared_stats_reset fills it as before.
template <typename T>
static T *lp_alloc(size_t bytes) {
  return static_cast<T *>(large_pages_alloc(bytes));
}

static void shared_stats_alloc(int threads) {
  int slots = 1;
  while (slots < threads)
    slots <<= 1;
  if (slots == g_stats.slots)
    return;
  large_pages_free(g_stats.follow);
  large_pages_free(g_stats.pawn_rows);
  large_pages_free(g_stats.corr);
  large_pages_free(g_stats.corr_x);
  g_stats.follow    = lp_alloc<PieceToRow[2][NO_PC + 1][64]>(sizeof(PieceToRow[2][2][NO_PC + 1][64]));
  g_stats.pawn_rows = lp_alloc<PieceToRow>(sizeof(PieceToRow) * PAWN_ROWS_PER_SLOT * slots);
  g_stats.corr      = lp_alloc<CorrSlot[2]>(sizeof(CorrSlot[2]) * CORR_ROWS_PER_SLOT * slots);
  g_stats.corr_x    = lp_alloc<int16_t[2]>(sizeof(int16_t[2]) * CORR_ROWS_PER_SLOT * slots);   // 256 KB per slot
  g_stats.pawn_mask = (U64)PAWN_ROWS_PER_SLOT * slots - 1;
  g_stats.corr_mask = (U64)CORR_ROWS_PER_SLOT * slots - 1;
  g_stats.slots     = slots;
#ifdef TRIUMV_C2
  if (!g_stats.struct_rows)   // --- C2 ordering (agent ORDER) --- fixed size: allocated once
    g_stats.struct_rows = lp_alloc<PieceToRow>(sizeof(PieceToRow) * C2_STRUCT_ROWS);
#endif
}

template <typename T>
static inline void fill_with(T *first, size_t count, int16_t value) {
  int16_t *p = reinterpret_cast<int16_t *>(first);
  std::fill(p, p + count * (sizeof(T) / sizeof(int16_t)), value);
}

// Starting values of the tables at a new game: parameters HistInit* since the CENSUS of 21/10/2026 (were -586, -1338,
// -5, -5, -742, 5).
static void shared_stats_reset() {
  fill_with(&g_stats.follow[0][0][0][0], (size_t)2 * 2 * (NO_PC + 1) * 64, (int16_t)HistInitFollow);
  fill_with(g_stats.pawn_rows, (size_t)g_stats.pawn_mask + 1, (int16_t)HistInitPawn);
  fill_with(&g_stats.corr[0][0], (size_t)(g_stats.corr_mask + 1) * 2, (int16_t)HistInitCorr);
  fill_with(&g_stats.corr_x[0][0], (size_t)(g_stats.corr_mask + 1) * 2, 0);
#ifdef TRIUMV_C2
  fill_with(g_stats.struct_rows, (size_t)C2_STRUCT_ROWS, 0);   // --- C2 ordering (agent ORDER) ---
#endif
}

static void thread_stats_reset(ThreadData &td) {
  fill_with(&td.quiet_hist[0][0], (size_t)2 * QH_CTX_SIZE, (int16_t)HistInitQuiet);
  fill_with(&td.low_ply_hist[0][0], (size_t)LOW_PLY_SIZE * QH_SIZE, 0);
  fill_with(&td.capt_hist[0][0][0], (size_t)12 * 64 * 7, (int16_t)HistInitCapt);
  fill_with(&td.cont_corr[0][0], (size_t)(NO_PC + 1) * 64, (int16_t)HistInitContCorr);
  td.tt_move_hist = 0;
}

// Time-management state that lives between one move of the game and the next (13_deepen.inc).
static void clock_memory_reset();

void search_clear() {
  for (auto &td : thread_data) {
    thread_stats_reset(td);
#ifdef TRIUMV_C2
    // 21/10/2026: the pawn-king cache restarts with each game too. It holds exact outputs, so no evaluation changes,
    // but the options that read it on hits only (CorrPkW with CorrPkCompute 0, C2StructHistW) depended on what the
    // previous searches had left in it: a second bench in the same process gave another count.
    nn_pk_cache_clear(td.nnpos);
#endif
  }
  shared_stats_reset();
  clock_memory_reset();
}

// ---------------------------------------------------------------------------
// Thread
// ---------------------------------------------------------------------------
// AA3 (10/10/2026, speed, same board): the make copies here, before touching them, the 64 contiguous bytes of
// ThreadData from hash_key to plies_from_null (key, six partial keys, fifty-move counter, plies since the null move)
// and the three occupancies; the unmake restores them with two copies instead of recomputing them (td_keys_update: six XORs in memory
// read and rewritten, td_occ_update with the branch on the capture). Like SF's StateInfo, which do_move copies and undo_move
// leaves behind.
struct UndoInfo {
  int captured_piece;
  int captured_square;
  int old_castle;
  int old_enpassant;
  U64 keys[8];   // hash_key, pawn_key, mm_key[2], np_key[2], key_sink, fifty | plies_from_null
  U64 occ[3];
};
static_assert(offsetof(ThreadData, plies_from_null) + sizeof(int) == offsetof(ThreadData, hash_key) + sizeof(UndoInfo::keys) &&
              offsetof(ThreadData, fifty) == offsetof(ThreadData, hash_key) + 56,
              "AA3: hash_key ... plies_from_null devono essere 64 byte contigui in ThreadData");

static void init_cycle_tables();
static void nn_dirty_init();

void init_threads(int thread_count) {
  if (thread_count < 1)
    thread_count = 1;
  if (thread_count > MAX_THREADS)
    thread_count = MAX_THREADS;

  for (auto &td : thread_data)
    if (td.nnpos) {
      nn_pos_destroy(td.nnpos);
      td.nnpos = nullptr;
      td.nnstack = nullptr;
    }

  static bool cycle_ready = false;
  if (!cycle_ready) {
    init_cycle_tables();
    nn_dirty_init();   // rays for the uncovered threats of the network's dirty entries (06_nndirty.inc)
    cycle_ready = true;
  }

  num_threads = thread_count;
  thread_data.resize(num_threads);
  shared_stats_alloc(num_threads);
  for (int i = 0; i < num_threads; i++) {
    ThreadData &td = thread_data[i];
    td.thread_id = i;
    td.nnpos = nn_pos_create();
    td.nnstack = nn_pos_stack(td.nnpos);
    td.nodes = td.tb_hits = 0;
    td.best_move = td.best_reply = td.depth = 0;
    td.completed_depth = 0;
  }
  search_clear();
}

void copy_board_to_thread(ThreadData &td) {
  memcpy(td.bitboards, bitboards, sizeof(bitboards));
  memcpy(td.occupancies, occupancies, sizeof(occupancies));
  td.side = side;
  td.enpassant = enpassant;
  td.castle = castle;
  td.hash_key = hash_key;
  td.fifty = fifty;
  // Mailbox and partial keys: computed here in full, then maintained by make/unmake.
  for (int sq = 0; sq < 64; sq++)
    td.piece_on[sq] = -1;
  td.pawn_key = 0;
  td.mm_key[0] = td.mm_key[1] = 0;
  td.np_key[white] = td.np_key[black] = 0;
  for (int pc = P; pc <= k; pc++) {
    U64 bb = td.bitboards[pc];
    while (bb) {
      const int sq = get_ls1b_index(bb);
      td.piece_on[sq] = pc;
      const int type = pc % 6;
      if (type == P)
        td.pawn_key ^= piece_keys[pc][sq];
      else
        td.np_key[pc < p ? white : black] ^= piece_keys[pc][sq];
      if (type == N || type == B)
        td.mm_key[0] ^= piece_keys[pc][sq];
      else if (type == R || type == Q)
        td.mm_key[1] ^= piece_keys[pc][sq];
      pop_bit(bb, sq);
    }
  }
  td.plies_from_null = 1024;
  td.seldepth = 0;
  memcpy(td.repetition_table, repetition_table, sizeof(repetition_table));
  td.repetition_index = repetition_index;
  td.ply = 0;
  td.see_pin_key = ~td.hash_key;   // SeePinned: pins to be recomputed at the first SEE
#ifdef TRIUMV_C2
  td.c2_see_pair = -1;             // --- C2 piece values (agent SEE) ---: regime table rebuilt at the first use
#endif
}
