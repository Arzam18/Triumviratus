#pragma once
#ifndef MAGIC_H
#define MAGIC_H

#include "defs.h"

extern unsigned int random_state;
extern U64 generate_magic_number();
extern void init_magic_numbers();
extern U64 get_random_U64_number();
extern U64 find_magic_number(int square, int relevant_bits, int bishop);
extern void init_sliders_attacks(int bishop);
// ⛔ 04/10/2026 — PROVATE E TOLTE come inline in questo header: con la build PGO + ThinLTO +1,27% istruzioni e
// +0,77% cicli per nodo (xperf, 6 giri, nodi identici). Il compilatore le inlineava gia' dove rende; forzarle ovunque
// gonfia il codice caldo. Restano in magic.cpp.
// ⛔ 04/10/2026 notte — PROVATE E TOLTE le tabelle PER LINEA (traversa con shift, colonna e diagonali con PEXT su <= 6
// bit: 4 x 64 x 64 x 8 = 128 KB al posto dei 2,25 MB delle tabelle fancy): due letture piccole invece di una grande,
// nodi identici, ma cicli +0,55% e istruzioni +0,33% (xperf, 6 giri, PGO). Le tabelle grandi non mancano in cache
// quanto sembra: le righe lette davvero sono poche e calde (stessa lezione del 10/09 sulle tabelle impacchettate).
extern U64 get_bishop_attacks(int square, U64 occupancy);
extern U64 get_rook_attacks(int square, U64 occupancy);
extern U64 get_queen_attacks(int square, U64 occupancy);

#endif
