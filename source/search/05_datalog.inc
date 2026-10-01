// search/05_datalog.inc -- Data log, controllo dei parametri congelati, log delle ricerche.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).

void set_data_log_file(const char *path) {
  if (path && *path)
    g_data_log_file = path;
}

#ifdef TRIUMV_FROZEN
// Confronta i letterali congelati con le variabili vive. Gira una volta per
// ricerca e scorre poche centinaia di interi: invisibile accanto a un albero da
// milioni di nodi. Riporta ogni parametro una volta sola, altrimenti a ogni
// mossa riempirebbe il log della GUI con la stessa riga.
void triumv_frozen_check() {
    static bool reported[512] = {false};
    for (int i = 0; i < g_frozen_n && i < 512; i++) {
        if (reported[i]) continue;
        const TriumvFrozenRef &r = g_frozen_refs[i];
        long long live = r.isBool ? (long long)*(const bool *)r.p
                                  : (long long)*(const int *)r.p;
        if (live != r.val) {
            reported[i] = true;
            printf("info string ATTENZIONE: %s e' congelato a %lld ma e' stato "
                   "impostato a %lld: questa build IGNORA il cambiamento\n",
                   r.name, r.val, live);
            fflush(stdout);
        }
    }
    nn_frozen_check();   // stesse verifiche per le costanti della eval (nnue_bridge.cpp)
}
#endif

static void log_search_record(int best_move, int score, int depth) {
  // Per-process file (".<pid>") so parallel self-play instances never
  // interleave/corrupt one another. Merge later: cat <DataFile>.* > all.txt
#ifdef _WIN32
  unsigned long tri_pid = (unsigned long)GetCurrentProcessId();
#else
  unsigned long tri_pid = (unsigned long)getpid();
#endif
  std::string path = g_data_log_file + "." + std::to_string(tri_pid);
  std::ofstream out(path, std::ios::app | std::ios::binary); // pure '\n'
  if (!out)
    return;
  out << board_to_fen() << '\t' << move_to_uci(best_move) << '\t' << score
      << '\t' << depth << '\n';
}
U64 hash_entries = 0;
bool g_tt_pow2 = false;
U64 g_tt_diag_mask = 0;   // [DIAGNOSTICA]   // fissato da init_hash_table (vedi tt.h)
int current_age = 0;

extern U64 nodes;

std::vector<std::thread> search_threads;
std::vector<ThreadData> thread_data;
std::atomic<bool> stop_threads(false);

// A5 FIX 2026-07-25 — pavimento anti-forfeit del taglio DURO.
// Il taglio su `stoptime` puo' scattare prima che il thread 0 abbia UNA mossa di
// radice (optimum puo' valere pochi ms: tempo residuo minimo, Move Overhead che
// mangia tutto, o il confronto sul tempo che sbaglia nella finestra di overflow
// int32 di GetTickCount, ogni 49.7 giorni di uptime). In quel caso la ricerca si
// smonta con best_move = 0 e il rescue di last-resort emette la PRIMA MOSSA
// LEGALE, cioe' una mossa a caso: si perde la posizione invece del tempo.
// Rimandiamo il taglio duro finche' non esiste una mossa vera. La finestra e'
// limitata a UNA mossa di radice a depth 1 (start_depth = 1 per il thread 0),
// quindi non puo' causare un forfeit. Stessa scelta di Reckless (time.rs:81-84).
// Costo: ZERO nel path caldo — grazie allo short-circuit questa funzione viene
// valutata solo DOPO che `get_time_ms() > stoptime` e' gia' vero, cioe' una volta
// per ricerca. Il taglio da comando `stop` e da g_node_limit non passa da qui.
static inline bool td_root_move_ready() {
  return !thread_data.empty() && (thread_data[0].best_move != 0 ||
                                  thread_data[0].pv_table[0][0] != 0);
}
U64 g_node_limit =
    0; // "go nodes N" budget (0 = off); checked per-node in (q)search.
// AUDIT D (T9/S6, 27/09/2026): il controllo per nodo guarda i nodi del SINGOLO thread, quindi con N thread
// `go nodes X` cercava ~N*X nodi. Ogni thread riceve X/N (fissato in search_position_mt); il primo che esaurisce
// la sua quota ferma tutti. A 1 thread e' X: identico.
static U64 g_node_limit_thread = 0;
// AUDIT D (T4, 27/09/2026): con `go infinite` la ricerca finiva da sola a profondita' 64 (o subito con la
// scorciatoia TB alla radice) e stampava bestmove senza aspettare `stop`, contro il protocollo UCI.
// g_uci_stop distingue lo `stop` venuto da fuori (stop_search_threads) dagli stop interni della ricerca.
bool g_go_infinite = false;
static std::atomic<bool> g_uci_stop{false};
// PONDER (01/10/2026). `go ponder`: la ricerca parte sulla posizione dopo la risposta prevista, col budget di tempo
// gia' calcolato da parse_go ma SPENTO (timeset = 0, istanti parcheggiati qui da ponder_park_time). `ponderhit` lo
// riaccende con gli STESSI istanti assoluti, misurati dal `go ponder` come in Stockfish: il tempo pensato in ponder
// conta, e se ha gia' superato il budget la mossa esce subito (orologio risparmiato). Finche' g_pondering e' vero la
// bestmove non si stampa (come `go infinite`): esce solo dopo ponderhit o stop.
bool g_go_ponder = false;                 // da parse_go per la prossima launch_search; azzerato da reset_time_control
static std::atomic<bool> g_pondering{false};
static int g_ponder_timeset = 0, g_ponder_soft = 0, g_ponder_stop = 0;
// searchmoves (analisi, UCI "go searchmoves m1 m2 ..."): whitelist di mosse di
// root. count>0 => alla root si cercano SOLO queste (le altre saltate nel move
// loop). Vale anche per le linee MultiPV. Riempita da parse_go, azzerata a ogni
// nuovo 'go'.
int g_searchmoves[256];
int g_searchmoves_count = 0;
// "go mate N": fermati appena un'iterazione completata conferma un matto NOSTRO
// in <= N mosse (0 = off). Convenzione UCI; il display mate M usa la stessa
// formula.
int g_mate_in = 0;
std::mutex output_mutex;
int num_threads = 1;
int search_start_time = 0;
int soft_time_limit = 0;
