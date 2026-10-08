// search/02_state.inc -- Stato globale della ricerca: thread, arresto, limiti, uscita, registro delle partite.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'.

std::vector<std::thread> search_threads;
ThreadDataVec            thread_data;   // su large pages (V1, threads.h)
std::atomic<bool>        stop_threads(false);
std::mutex               output_mutex;
int                      num_threads = 1;
SearchLimits             g_limits;

// Ricerca di sole patte/vittorie: valori e soglie. I matti stanno in (mate_score, mate_value], le vittorie da
// tablebase in [TB_WIN_FLOOR, TB_WIN_TOP] (syzygy.cpp: 30000 - max_ply - ply).
constexpr int VAL_NONE     = 32001;
constexpr int TB_WIN_TOP   = mate_score - max_ply;      // 29872
constexpr int TB_WIN_FLOOR = TB_WIN_TOP - max_ply;      // 29744: da qui in su lo score e' "deciso"
constexpr int MATE_FLOOR   = mate_value - max_ply;      // 30872: da qui in su e' un matto
constexpr int NULL_MOVE    = -1;                         // mossa nulla nei frame (0 = nessuna mossa)

static inline bool val_known(int v) { return v != VAL_NONE; }
static inline bool val_won(int v) { return v >= TB_WIN_FLOOR; }
static inline bool val_lost(int v) { return v <= -TB_WIN_FLOOR; }
static inline bool val_decided(int v) { return val_won(v) || val_lost(v); }
static inline bool val_mating(int v) { return v >= MATE_FLOOR || v <= -MATE_FLOOR; }
static inline int  mating_in(int ply) { return mate_value - ply; }
static inline int  mated_at(int ply) { return -mate_value + ply; }

// Stop richiesto da fuori (comando stop / quit), distinto dagli stop interni della ricerca.
static std::atomic<bool> g_uci_stop{false};
// Ponder: finche' e' vero il tempo non ferma la ricerca e la bestmove aspetta ponderhit o stop.
static std::atomic<bool> g_pondering{false};
static std::atomic<bool> g_stop_at_ponderhit{false};
// "go searchmoves": definite in defs.h, riempite da parse_go.
int  g_searchmoves[256];
int  g_searchmoves_count = 0;

// Pavimento anti-forfeit: il taglio a tempo aspetta che il thread 0 abbia almeno una mossa di radice cercata.
static inline bool root_move_ready() {
  return !thread_data.empty() && (thread_data[0].completed_depth > 0 || thread_data[0].root[0].score != -infinity);
}

// ---------------------------------------------------------------------------
// Registro delle partite (DataLog): una riga "FEN<TAB>mossa<TAB>score<TAB>profondita'" per ricerca.
// File per processo (".<pid>"), cosi' piu' istanze in parallelo non si sovrappongono.
// ---------------------------------------------------------------------------
void set_data_log_file(const char *path) {
  if (path && *path)
    g_data_log_file = path;
}

static void log_search_record(int best_move, int score, int depth) {
#ifdef _WIN32
  unsigned long tri_pid = (unsigned long)GetCurrentProcessId();
#else
  unsigned long tri_pid = (unsigned long)getpid();
#endif
  std::string path = g_data_log_file + "." + std::to_string(tri_pid);
  std::ofstream out(path, std::ios::app | std::ios::binary);
  if (!out)
    return;
  out << board_to_fen() << '\t' << move_to_uci(best_move) << '\t' << score << '\t' << depth << '\n';
}
