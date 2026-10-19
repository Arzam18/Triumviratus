// Triumviratus, a UCI chess engine.
// Copyright (C) 2026 Francesco Torsello
// SPDX-License-Identifier: GPL-3.0-or-later (see COPYING)

#pragma once
#ifndef CONSILIUM_BRIDGE_H
#define CONSILIUM_BRIDGE_H

// consilium_bridge.h -- the engine's interface to the Consilium network (block 6 of 8.1; until 18/10/2026
// nnue_bridge.h). The network is evaluated by our module in consilium/; the rest of the engine sees only these
// plain C entry points and the dirty-stack types of nn_dirty.h.

#include "nn_dirty.h"   // the changes of each move (plain types, shared with the network module)

// Tables built once at startup: the feature indices of the network module.
void nn_init_tables(void);

// Name of the default network (CN_DEFAULT_NET_FILE, defined in consilium_bridge.cpp). Exposed here because the rest
// of the engine does not see the network module: one source for the name, no copies to keep in step.
const char* nn_default_net_name(void);
// Network shared between processes (shared_net.h): the outcome of the attach, "" in builds without it.
const char* nn_net_memory_status(void);

// Load the Consilium network from a file path. Returns 1 on success (file opened, hashes verified), 0 otherwise.
// Called once at startup, before any nn_pos_create().
int nn_load_net(const char* net_path);

// Reload the network at runtime (UCI option "EvalFile"). Safe to call when no search is running. Returns 1 on
// success, 0 if the path could not be loaded.
int nn_reload_big(const char* net_path);

// No-ops kept for API stability: the refresh cache is always on, and "accstats" has no counters.
void nn_set_finny(int on);
void nn_acc_stats(void);

// Incremental evaluation (default on): accumulators updated from the parent state with the dirty stack. Off = a full
// refresh at every evaluation (diagnostic). Re-set the handle (nn_pos_set) after toggling.
void nn_set_incremental(int on);

// Diagnostic: compare the incremental evaluation with a full refresh at every evaluation (prints the first
// mismatches). Halves the speed; one thread recommended. Default off.
void nn_set_verify(int on);

// Evaluation scale in percent: matches the network's units to the search margins. UCI option "EvalScale" writes all
// eight output buckets; EvalScaleB0..B7 one each (bucket = (pieces - 1) / 4, the network's output bucket).
void nn_set_eval_scale(int pct);
void nn_set_eval_scale_bucket(int bucket, int pct);
int  nn_get_eval_scale_bucket(int bucket);   // current value (the default at startup), for the UCI line

// --- Constants of the evaluation scale ------------------------------------------------------------------------------
// The constants that turn the two network outputs (psqt, positional) into the evaluation: blend weights, damping by
// disagreement, material stretch, optimism terms and the mix of the fifty-move step. Tuned by our SPSA runs on this
// network (BAKE 09/09 and MOE1 30/09/2026). They change what the evaluation says, not only its size, which is the
// difference with EvalScale / EvalScaleB. The enumeration lets uci_mt.cpp declare the options without repeating names.
int         nn_eval_const_count(void);
const char* nn_eval_const_name(int i);
int         nn_eval_const_get(int i);
int         nn_eval_const_lo(int i);
int         nn_eval_const_hi(int i);
int         nn_set_eval_const(const char* name, int value);   // 1 if the name exists
#ifdef TRIUMV_FROZEN
void        nn_frozen_check(void);   // release builds: warns once when an option differs from the compiled value
#endif
int  nn_get_eval_scale(void);   // current EvalScale percent (to print "score cp" without it)
int  nn_last_opt_base(void* handle);    // last evaluation of the thread with optimism 0 (before fifty and scale)
int  nn_last_opt_coeff(void* handle);   // its optimism coefficient, in thousandths
int  nn_last_unadjusted(void* handle);  // last unadjusted value (before fifty and scale) of the thread
// The final evaluation from an unadjusted value: `bucket` = (pieces - 1) / 4 of the CURRENT position, which selects
// the per-bucket scale.
int  nn_finalize(int unadjusted, int rule50, int bucket);

// Evaluation from scratch (full refresh) of the engine's global board, for the "eval" command: bb[12] in the order
// P,N,B,R,Q,K,p,n,b,r,q,k, occ[3] = white/black/all, squares with a8 = 0 (the engine's numbering).
// Returns the evaluation (side to move, internal units == the engine's evaluation scale).
int nn_eval(int side_white, const unsigned long long* bb, const unsigned long long* occ, int rule50,
            int* raw_out = nullptr);

// ---------------------------------------------------------------------------------------------------------------------
// Per-thread handle: the dirty stack, the accumulators and the refresh cache. The network has no board of its own: it
// reads the engine's in nn_pos_eval. The engine's make writes the changes of each move on the handle's stack
// (nn_pos_stack, types in nn_dirty.h): one state per move made, removed at unmake; null moves add no state.
// ---------------------------------------------------------------------------------------------------------------------

// Create / destroy a per-thread handle (opaque).
void* nn_pos_create(void);
void  nn_pos_destroy(void* handle);
void  nn_pos_set_optimism(void* handle, int w, int b);   // OptPerThread

// Back to the root: the stack returns to one state, and the root accumulator is computed at the first evaluation.
void  nn_pos_set(void* handle);

// The handle's dirty stack, written directly by the engine's make and unmake.
NnStack* nn_pos_stack(void* handle);

// Memory hint from the make (board before the move, bb[12] as above; phase = material band after the move): asks for
// the network rows the move will change, so they are in cache when the child evaluates. Changes no state. Without a
// capture capPc/capSq repeat pc/from; a promotion arrives as toPc.
// The make calls it only where it pays (NN_MAKE_PREFETCH): xperf 18/10/2026, PGO, middlegame, -0.56% cycles per node on
// AVX2 and +2.8% on AVX-512 (there the update hides the latency by itself, and the call in every make costs more).
// So on in the AVX2 builds without AVX-512 (avx2, avx2-nopext, avxvnni); -DCN_MAKE_PREFETCH forces it on elsewhere,
// -DCN_NO_MAKE_PREFETCH turns it off.
#if !defined(CN_NO_MAKE_PREFETCH) && (defined(CN_MAKE_PREFETCH) || (defined(USE_AVX2) && !defined(USE_AVX512)))
    #define NN_MAKE_PREFETCH 1
#endif
void  nn_prefetch_move(const unsigned long long* bb, int phase, int pc, int from, int toPc, int to, int capPc,
                       int capSq);

// Evaluate the current position (internal units, side to move). The network reads the engine's board: bb[12] in the
// order P,N,B,R,Q,K,p,n,b,r,q,k, occ[3] = white/black/all, mailbox[64] with -1 = empty, all with a8 = 0. side_white and
// rule50 are the search's. The dirty states up to this position must be complete (the engine's nn_dirty_catch_up).
int   nn_pos_eval(void* handle, const unsigned long long* bb, const unsigned long long* occ, const int* mailbox,
                  int side_white, int rule50);

// |psqt - positional| of the last nn_pos_eval on this thread (19/10/2026): how much the two parts of the network
// disagree on the position, read by the search (RdDisagree). The caller resets it to -1 before an evaluation that may
// come from the hash instead of the network.
extern thread_local int nn_last_disagree;

// nnueverify: evaluations compared and evaluations that differed since startup.
unsigned long long nn_verify_count(void);
unsigned long long nn_verify_bad(void);

#endif // CONSILIUM_BRIDGE_H
