// Triumviratus, a UCI chess engine.
// Copyright (C) 2026 Francesco Torsello
// SPDX-License-Identifier: GPL-3.0-or-later (see COPYING)

#pragma once
#ifndef CONSILIUM_BRIDGE_H
#define CONSILIUM_BRIDGE_H

// consilium_bridge.h -- the engine's interface to the Consilium network (block 6 of 8.1; until 18/10/2026
// nnue_bridge.h). The network is evaluated by our module in consilium/; the rest of the engine sees only these
// plain C entry points and the dirty-stack types of nn_dirty.h.

#include "nn_dirty.h"   // le dirty di ogni mossa (tipi semplici, condivisi con la rete)

// Initialize the shared substrate tables: bitboards + slider-magic attacks.
// (Attacks::init() is SEPARATE from Bitboards::init() in the master.)
void nn_init_tables(void);

// Nome del net di default (macro EvalFileDefaultName, definita in consilium_bridge.cpp).
// Esposto qui perche' il resto del motore non vede il modulo della rete: un solo
// punto di verita' per il nome, niente stringhe duplicate da tenere in sync.
const char* nn_default_net_name(void);
// Rete condivisa fra processi (-DTRIUMV_SHARED_NET): esito della attach, "" nelle altre build.
const char* nn_net_memory_status(void);

// Load the Consilium network from a file path. Returns 1 on success (file opened +
// arch-hash verified), 0 otherwise. Must be called once at startup before any
// nn_pos_create() (the per-handle accumulator caches are built from the net).
int nn_load_net(const char* net_path);

// Reload the network at runtime (UCI option "EvalFile"). Safe to call when no
// search is running. Returns 1 on success, 0 if the path could not be loaded.
int nn_reload_big(const char* net_path);

// No-ops kept for API stability: the refresh cache is always on, and "accstats" has no counters.
void nn_set_finny(int on);
void nn_acc_stats(void);

// Incremental evaluation (default on): accumulators updated from the parent state with the dirty stack. Off = a full
// refresh at every evaluation (diagnostic). Re-set the handle (nn_pos_set) after toggling.
void nn_set_incremental(int on);

// DEBUG: cross-check incremental == full-refresh at every leaf eval (prints the
// first mismatches). Halves NPS; single-thread recommended. Default OFF.
void nn_set_verify(int on);

// Eval output scale in percent: matches the network's units to the search margins. UCI option "EvalScale".
void nn_set_eval_scale(int pct);          // scrive TUTTI gli 8 bucket (comportamento storico)
// EvalScale per bucket di output (15/08/2026). bucket = (pezzi - 1) / 4, 0..7, come
// il bucket di uscita della rete. Dal BAKE MOE1 (30/09/2026) i default sono per fascia (rete MoE-1024), non piu' tutti 60.
void nn_set_eval_scale_bucket(int bucket, int pct);
int  nn_get_eval_scale_bucket(int bucket);   // valore corrente (all'avvio = default), per la riga UCI

// --- Costanti del blend dell'eval (15/08/2026) -------------------------------
// Le costanti che trasformano le due uscite della rete (psqt, positional) in una valutazione: pesi del blend,
// smorzamento per disaccordo, scala col materiale, termini dell'optimism, e la miscela del passo delle 50 mosse. Tarate dai nostri
// SPSA su questa rete (BAKE 09/09 e MOE1 30/09/2026). Non scalano l'eval: decidono cosa dice, che e' la differenza
// con EvalScale/EvalScaleB (scala per bucket).
// L'enumerazione serve a uci_mt.cpp per dichiarare le opzioni senza duplicare i nomi.
int         nn_eval_const_count(void);
const char* nn_eval_const_name(int i);
int         nn_eval_const_get(int i);
int         nn_eval_const_lo(int i);
int         nn_eval_const_hi(int i);
int         nn_set_eval_const(const char* name, int value);   // 1 se il nome esiste
#ifdef TRIUMV_FROZEN
void        nn_frozen_check(void);   // vedi COSTANTI DELLA MISCELA CONGELATE
#endif
int  nn_get_eval_scale(void);   // current EvalScale %% (per normalizzare 'score cp' in stampa)
int  nn_last_opt_base(void* handle);    // EvalCacheOptSplit: eval con optimism=0 (pre-rule50, post-scale)
int  nn_last_opt_coeff(void* handle);   // EvalCacheOptSplit: coefficiente in MILLESIMI dell'optimism
int  nn_last_unadjusted(void* handle);  // unadjusted (pre-rule50/scale) dell'ultima nn_scale (thread-local)
// Ricostruisce l'eval finale dall'unadjusted. `bucket` = (pezzi - 1) / 4 della posizione
// CORRENTE: serve a scegliere la scala per bucket, e qui non c'e' la Position.
int  nn_finalize(int unadjusted, int rule50, int bucket);

// Valutazione da zero (refresh completo) della scacchiera globale, per il comando "eval": bb[12] nell'ordine
// P,N,B,R,Q,K,p,n,b,r,q,k, occ[3] = bianco/nero/tutti, case a8 = 0 (la numerazione del motore, l'unica).
// Returns the evaluation (stm-relative, internal units == the engine's eval scale).
int nn_eval(int side_white, const unsigned long long* bb, const unsigned long long* occ, int rule50,
            int* raw_out = nullptr);

// ---------------------------------------------------------------------------
// Handle per thread (07/10/2026, scacchiera unica v2): pila delle dirty, accumulatori e cache dei refresh. La rete non tiene una
// scacchiera sua: legge quella del motore in nn_pos_eval. Le dirty di ogni mossa le scrive la make del motore sulla
// pila dell'handle (nn_pos_stack, tipi in nn_dirty.h): uno stato per mossa fatta, tolto alla unmake; le mosse nulle
// non aggiungono stati.
// ---------------------------------------------------------------------------

// Create / destroy a per-thread incremental position handle (opaque).
void* nn_pos_create(void);
void  nn_pos_destroy(void* handle);
void  nn_pos_set_optimism(void* handle, int w, int b);   // OptPerThread

// Riparte dalla radice: la pila torna a un solo stato e l'accumulatore della radice si calcola alla prima valutazione.
void  nn_pos_set(void* handle);

// La pila delle dirty dell'handle, che la make e la unmake del motore scrivono direttamente.
NnStack* nn_pos_stack(void* handle);

// Evaluate the current position (centipawns / internal units, stm-relative).
// La rete legge la NOSTRA scacchiera: bb[12] nell'ordine P,N,B,R,Q,K,p,n,b,r,q,k, occ[3] = bianco/nero/tutti,
// mailbox[64] con -1 = vuota, tutto con a8 = 0. side_white e rule50 sono quelli della ricerca. Le dirty delle mosse
// fino a questa posizione devono essere complete (nn_dirty_catch_up del motore, prima).
int   nn_pos_eval(void* handle, const unsigned long long* bb, const unsigned long long* occ, const int* mailbox,
                  int side_white, int rule50);

// Controllo nnueverify: valutazioni confrontate e valutazioni diverse dall'avvio.
unsigned long long nn_verify_count(void);
unsigned long long nn_verify_bad(void);

#endif // CONSILIUM_BRIDGE_H
