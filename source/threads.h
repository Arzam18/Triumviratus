#pragma once
#ifndef THREADS_H
#define THREADS_H

#include "defs.h"
#include "search.h"
#include <thread>
#include <vector>
#include <new>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>

// ============================================================================
// Ricerca di Triumviratus, riscritta il 04/10/2026 sulla logica della ricerca di Stockfish 19 (GPLv3).
// Strutture, nomi e codice sono nostri; le regole e i numeri seguono SF19 (vedi
// docs/audit_8.0/L_RISCRITTURA_RICERCA.md). La ricerca precedente e' in
// _backup/Triumviratus_8.0_pre_riscrittura_ricerca_2026-10-04.
// ============================================================================

#define MAX_THREADS 512

// Dimensioni delle tabelle di history.
constexpr int QH_SIZE       = 64 * 64 * 5;   // indice della mossa: da | a<<6 | tipo di promozione<<12
constexpr int LOW_PLY_SIZE  = 5;             // ply vicini alla radice con la history dedicata
constexpr int NO_PC         = 12;            // "nessun pezzo": blocco sentinella delle continuation
constexpr int CAPT_NONE     = 6;             // tipo catturato per le promozioni senza cattura e l'e.p.

// Una tabella pezzo x casa d'arrivo: e' la "riga" a cui puntano le continuation history.
typedef int16_t PieceToRow[12][64];

// Stato di un ply del cammino corrente.
struct NodeFrame {
    PieceToRow* cont_hist;        // continuation history scelta dalla mossa che porta al figlio
    PieceToRow* cont_corr;        // continuation correction history, stessa chiave
    int  ply;
    int  move;                    // mossa fatta a questo ply (0 = nessuna, NULL_MOVE = mossa nulla)
    int  captured;                // pezzo catturato da quella mossa (P..k), -1 = nessuno
    int  excluded;                // mossa esclusa (ricerca singolare)
    int  static_eval;
    int  stat_score;
    int  move_count;
    int  cutoff_cnt;
    int  reduction;
    int  pieces;                  // pezzi sulla scacchiera in questo nodo (PhaseEdge: esperto della rete)
    bool in_check;
    bool tt_pv;
    bool tt_hit;
    bool follow_pv;
};

// Una mossa di radice con la sua linea e le statistiche raccolte su di essa.
struct RootLine {
    int       pv[max_ply + 4];
    int       pv_len;
    int       prev_pv[max_ply + 4];
    int       prev_pv_len;
    int       score;
    int       prev_score;
    int       avg_score;
    long long mean_sq;
    int       uci_score;
    bool      lower;              // lo score e' solo un limite inferiore (fail-high in finestra)
    bool      upper;              // solo un limite superiore
    bool      prev_exact;
    int       sel_depth;
    U64       effort;             // nodi spesi sotto questa mossa
};

// Limiti della ricerca, scritti da parse_go (uci_mt.cpp).
struct SearchLimits {
    int  time[2];
    int  inc[2];
    int  movestogo;
    int  movetime;
    int  depth;
    int  mate;
    U64  nodes;
    bool infinite;
    bool ponder;
    int  start_ms;
    bool use_tm() const { return time[0] || time[1]; }
};
extern SearchLimits g_limits;

constexpr int FRAME_OFFSET = 7;   // frame[FRAME_OFFSET] e' la radice; 7 frame sentinella prima

struct ThreadData {
    int thread_id;

    // Scacchiera del thread
    U64 bitboards[12];
    U64 occupancies[3];
    int piece_on[64];             // pezzo su ogni casa (P..k), -1 = vuota
    int side;
    int enpassant;
    int castle;
    U64 hash_key;
    // Chiavi parziali: td_keys_update (07_makemove.inc) le vede come un array contiguo di sei U64 a partire da
    // pawn_key (pedoni, minori, maggiori, non-pedoni bianchi e neri, pozzo) e vi scrive con indici da tabella,
    // senza salti sul tipo di pezzo. L'ordine qui sotto e' vincolato da static_assert.
    U64 pawn_key;
    U64 mm_key[2];                // [0] = pezzi minori (N,B,n,b), [1] = maggiori (R,Q,r,q)
    U64 np_key[2];                // non-pedoni per colore, re compreso
    U64 key_sink;                 // pozzo: riceve gli XOR che non appartengono a nessuna chiave (mai letto)
    int fifty;
    int plies_from_null;
    int seldepth;

    U64 repetition_table[2048];
    int repetition_index;

    int ply;
    U64 nodes;
    U64 tb_hits;

    // Cammino corrente
    NodeFrame frames[max_ply + 16];
    int pv_length[max_ply + 8];
    int pv_table[max_ply + 8][max_ply + 8];
    signed char chk_hint[max_ply + 8];   // scacco al figlio gia' calcolato dal padre (-1 = ignoto)

    // Radice
    RootLine root[256];
    int  root_count;
    int  pv_idx;
    int  pv_last;
    int  root_depth;
    int  completed_depth;
    int  root_delta;
    int  sel_depth;
    int  nmp_min_ply;
    U64  best_move_changes;
    int  last_pv[max_ply + 4];        // linea dell'iterazione precedente per la mossa pv_idx
    int  last_pv_len;
    int  opt[2];                      // optimism di questo thread (lato bianco, nero)
    int  root_side;                   // colore al tratto alla radice (Contempt)
    int  deep_w;                      // peso 0..1024 dei termini Deep* per l'iterazione corrente
    int  eval_cplx;                   // psqt - positional dell'ultima valutazione (Disagree*), 0 se ignoto

    // Risultato, letto dal driver e dal voto fra thread
    int best_move;
    int best_score;
    int depth;
    int best_reply;

    // History di questo thread (le continuation, pawn e correction sono condivise: vedi 09_history.inc)
    int16_t    quiet_hist[2][QH_SIZE];
    int16_t    low_ply_hist[LOW_PLY_SIZE][QH_SIZE];
    int16_t    capt_hist[12][64][7];
    PieceToRow cont_corr[NO_PC + 1][64];
    int        tt_move_hist;

    // Rete incrementale e cache della valutazione
    void* nnpos = nullptr;
    static constexpr int EVAL_CACHE_BITS = 16;
    static constexpr int EVAL_CACHE_SIZE = 1 << EVAL_CACHE_BITS;
    static constexpr U64 EVAL_CACHE_MASK = EVAL_CACHE_SIZE - 1;
    struct EvalCacheEntry { U64 key; int eval; int cplx; };   // cplx sta nel riempimento: 16 byte come prima
    EvalCacheEntry eval_cache[EVAL_CACHE_SIZE];
};

// Global thread management
extern std::vector<std::thread> search_threads;
extern std::vector<ThreadData> thread_data;
extern std::atomic<bool> stop_threads;
extern int num_threads;

extern void init_threads(int thread_count);
extern void copy_board_to_thread(ThreadData& td);
extern void stop_search_threads();
extern void wait_for_threads();
extern void search_position_mt(int depth);
extern std::thread search_master;
extern void launch_search(int depth);
extern void ponder_hit();
extern void wait_for_search_done();
extern void search_clear();            // ucinewgame: azzera history e stato della partita

extern void set_data_log_enabled(bool enabled);
extern void set_data_log_file(const char* path);

extern int debug_eval_position();
extern int debug_eval_position_raw();

// Opzioni UCI della ricerca: stampa e impostazione (01_params.inc).
extern void print_search_options();
extern bool set_search_param(const char* name, int value);

#endif
