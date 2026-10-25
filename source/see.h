#pragma once
#ifndef SEE_H
#define SEE_H

#include "defs.h"
#include "threads.h"

// Piece values for SEE
// 04/10/2026: values in the units of the search margins (pawn 208), the same as unit_value in
// search/09_history.inc. Before 100/320/330/500/900.
// 21/10/2026 (agent SEE): constexpr, so that the release can check the parameters SeeVal* against it (01_params.inc).
static constexpr int see_piece_values[12] = {
    208, 781, 825, 1276, 2538, 20000,  // P N B R Q K
    208, 781, 825, 1276, 2538, 20000   // p n b r q k
};

// --- C2 piece values (agent SEE) --- (21/10/2026; OFF by default; SPRT/SPSA pending)
// SEE piece values as parameters (SeeValP..SeeValQ, search/01_params.inc): the development builds read the copy
// g_see_val, synced at setoption like g_see_pinned; the release reads the constant table above (identical values,
// static_assert in 01_params.inc). Regime piece values (C2SeeReg, C2Val*, Consilium Plebis builds only): see see.cpp.
#ifndef TRIUMV_RELEASE
extern int g_see_val[12];          // SeeValP..SeeValQ for both colours, kings 20000
#endif
#ifdef TRIUMV_C2
// Offsets of the regime piece values for the current position of the thread, indexed by piece code (16 entries:
// pawns, kings and codes 12..15, i.e. "no piece" & 15, are 0). Refreshed when the expert pair changes (see.cpp).
const int* see_c2_offsets(const ThreadData& td);
#endif

// Static Exchange Evaluation for thread-local state
int td_see(ThreadData& td, int move);

// F-008: THRESHOLD SEE with early exit (SF see_ge style). Returns 1 if
// td_see(move) >= threshold, 0 otherwise - without simulating the full exchange.
int td_see_ge(ThreadData& td, int move, int threshold);

#endif
