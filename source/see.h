#pragma once
#ifndef SEE_H
#define SEE_H

#include "defs.h"
#include "threads.h"

// Piece values for SEE
// 04/10/2026: valori nelle unita' dei margini della ricerca (pedone 208), gli stessi di unit_value in
// search/09_history.inc. Prima 100/320/330/500/900.
static const int see_piece_values[12] = {
    208, 781, 825, 1276, 2538, 20000,  // P N B R Q K
    208, 781, 825, 1276, 2538, 20000   // p n b r q k
};

// Static Exchange Evaluation for thread-local state
int td_see(ThreadData& td, int move);

// F-008: SEE a SOGLIA con early-exit (stile SF see_ge). Ritorna 1 se
// td_see(move) >= threshold, 0 altrimenti — senza simulare lo scambio completo.
int td_see_ge(ThreadData& td, int move, int threshold);

#endif
