/*
 * TRIUMVIRATUS - Lazy SMP Multi-threaded Search
 *
 * Each thread runs an independent alpha-beta search, sharing only the
 * transposition table. Helper threads (id > 0) diversify their effort via
 * per-thread iterative-deepening depth skipping (LSMP_Skip* tables); the main
 * thread (id 0) drives time management and the PV. The legacy ABDADA busy-node
 * coordination was removed once Lazy SMP proved a clear win (+55 Elo @4CPU).
 */

#include "threads.h"
#include "attacks.h"
#include "chess960.h"
#include "evaluation.h"
#include "magic.h"
#include "misc.h"
#include "movegen.h"
#include "nnue_bridge.h"
#include "search.h"
#include "see.h"
#include "tt.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifndef _WIN32
#include <unistd.h> // getpid() (su Windows il pid arriva da GetCurrentProcessId in windows.h)
#endif
#include "io.h"
#include "profile.h"
#include <fstream>
#include <string>

#ifdef TRIUMV_PROFILE
unsigned long long prof_eval = 0, prof_mg = 0, prof_make = 0, prof_tt = 0,
                   prof_score = 0;
unsigned long long prof_ft = 0, prof_fc0 = 0, prof_layers = 0;
unsigned long long prof_catchup = 0;
unsigned long long prof_feat_hist[PROF_FEAT_N] = {};
unsigned long long prof_psq_hist[PROF_PSQ_N]   = {};
unsigned short*    prof_cooc                   = nullptr;
unsigned long long prof_acc_inc = 0, prof_acc_refresh = 0, prof_ft_out = 0;
unsigned long long prof_n_inc = 0, prof_n_refresh = 0, prof_n_eval = 0;
unsigned long long prof_n_cols = 0, prof_n_upd = 0;
unsigned long long prof_n_thr_seen = 0, prof_n_thr_dead = 0;
unsigned long long prof_max_active = 0, prof_max_inc = 0;
unsigned long long prof_cols_thr = 0, prof_cols_pawn = 0, prof_n_refresh_calls = 0;
unsigned long long prof_cols_psq_inc = 0, prof_cols_thr_inc = 0, prof_cols_pawn_inc = 0;
unsigned long long prof_mp = 0, prof_hist = 0, prof_corr = 0, prof_gc = 0, prof_n_mg = 0;
unsigned long long prof_thr = 0, prof_see = 0, prof_isatk = 0, prof_rep = 0;
unsigned long long prof_idx_thr = 0, prof_idx_pawn = 0;
unsigned long long prof_n_thr_calls = 0, prof_n_see = 0, prof_n_isatk = 0;
unsigned long long prof_pawn_hit = 0, prof_pawn_miss = 0;
unsigned long long prof_refresh_same_orient = 0, prof_refresh_cross_orient = 0;
unsigned long long prof_dead_pair[8][8] = {};
#endif
#include "defs.h"

#include "syzygy.h"

// ============================================================================
// threads.cpp -- ricerca multi-thread. DIVISO il 27/09/2026 in search/*.inc (era un file da 12.000 righe).
// Resta UNA sola unita' di compilazione: i pezzi sono inclusi qui, nell'ordine originale, perche'
//   - molte funzioni del percorso caldo sono `static inline` e vanno viste dal compilatore insieme ai chiamanti;
//   - le #define di TRIUMV_FROZEN (search/04_frozen.inc) valgono per tutto il codice che segue.
// Verifica della divisione: threads.o identico byte per byte prima e dopo (g++, stessi flag), bench 273477.
// I riferimenti "threads.cpp:NNNN" nei commenti vecchi si riferiscono alla numerazione prima della divisione.
// ============================================================================
#include "search/01_params_globals.inc"
#include "search/02_params_candidates.inc"
#include "search/03_params_study.inc"
#include "search/04_frozen.inc"
#include "search/05_datalog.inc"
#include "search/06_init.inc"
#include "search/07_makemove.inc"
#include "search/08_movegen.inc"
#include "search/09_eval_scoring.inc"
#include "search/10_picker.inc"
#include "search/11_qsearch.inc"
#include "search/12_negamax.inc"
#include "search/13_iterdeep.inc"
#include "search/14_smp.inc"
#include "search/15_tdperft.inc"
