#pragma once
// Chess960 (Fischer Random). Vedi docs/audit_7.1/I_CHESS960.md.
//
// Fase 1 (27/09/2026): modello dati letto da parse_fen (X-FEN e Shredder-FEN).
// Fase 2 (27/09/2026): collegato al motore. Il motore codifica l'arrocco come prima (re verso g1/c1/g8/c8 col
// flag di arrocco; in 960 il re puo' anche non muoversi, sorgente == destinazione) e prende la casa della torre
// da castle_rook_sq[], che negli scacchi normali vale sempre h1/a1/h8/a8. Con g_chess960 spento (default, e
// unico stato raggiungibile in release) la generazione usa il codice di sempre e l'albero e' identico.
#include "defs.h"

extern bool g_chess960;              // UCI_Chess960

struct Castling960 {
    int rights;                      // bit wk/wq/bk/bq letti dalla FEN
    int king_from[2];                // casa del re [white, black]
    int rook_from[4];                // casa della torre per wk, wq, bk, bq (no_sq se il diritto manca)
    int rights_mask[64];             // castle &= rights_mask[sq] per origine e destinazione della mossa
};
extern Castling960 g_c960;

// Case d'arrocco usate dal motore (make/unmake, generazione, stampa delle mosse). Scacchi normali: h1 a1 h8 a8
// ed e1 e8. Riscritte da parse_fen: da g_c960 col 960 acceso, ai valori normali altrimenti.
extern int castle_rook_sq[4];
extern int castle_king_sq[2];

// Legge il campo arrocco di una FEN (X-FEN: K/Q/k/q = torre piu' esterna su quel lato; Shredder-FEN:
// A-H/a-h = colonna della torre) sulla scacchiera GIA' caricata. Diritti incoerenti (niente re sulla
// traversa, niente torre sulla colonna) vengono scartati. Ritorna il numero di diritti scartati.
int c960_parse_castling(const char* field_begin, const char* field_end);

// Porta nel motore le case e la maschera dei diritti: da g_c960 (960) o i valori normali (standard).
void c960_apply_to_engine();
void c960_reset_standard();

// Indice 0..3 del diritto (wk, wq, bk, bq) -> bit.
static inline int c960_bit(int idx) { return 1 << idx; }

// Indice del diritto dalla casa d'arrivo del RE (g1, c1, g8, c8), la codifica interna dell'arrocco.
static inline int castle_index(int king_to) {
    return king_to == g1 ? 0 : king_to == c1 ? 1 : king_to == g8 ? 2 : 3;
}
static inline int castle_rook_to(int idx) {
    return idx == 0 ? f1 : idx == 1 ? d1 : idx == 2 ? f8 : d8;
}
static inline int castle_king_to(int idx) {
    return idx == 0 ? g1 : idx == 1 ? c1 : idx == 2 ? g8 : c8;
}

// Case fra a e b comprese, sulla stessa traversa (indici contigui: a8 = 0 ... h1 = 63).
static inline U64 c960_span(int a, int b) {
    const int lo = a < b ? a : b, hi = a < b ? b : a;
    return ((hi == 63 ? 0ULL : (1ULL << (hi + 1))) - (1ULL << lo));
}

// Regola dell'arrocco 960 (come SF). Diritto idx presente; libere le case fra re e sua destinazione e fra
// torre e sua destinazione, esclusi re e torre stessi; nessuna casa del percorso del re attaccata, casa di
// partenza compresa (non si arrocca sotto scacco). La casa d'arrivo la verifica la legalita' dopo make, con
// la torre gia' spostata: copre anche il caso della torre che schermava un attacco sulla traversa.
template <typename Attacked>
static inline bool c960_can_castle(int castle, int idx, U64 occ, Attacked attacked) {
    if (!(castle & c960_bit(idx))) return false;
    const int ksq = castle_king_sq[idx >> 1], rsq = castle_rook_sq[idx];
    const int kto = castle_king_to(idx), rto = castle_rook_to(idx);
    const U64 movers = (1ULL << ksq) | (1ULL << rsq);
    if (occ & (c960_span(ksq, kto) | c960_span(rsq, rto)) & ~movers) return false;
    U64 path = c960_span(ksq, kto) & ~(1ULL << kto);
    if (ksq == kto) path = 1ULL << ksq;
    while (path) {
        const int sq = get_ls1b_index(path);
        if (attacked(sq)) return false;
        path &= path - 1;
    }
    return true;
}
