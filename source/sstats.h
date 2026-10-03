// sstats.h -- contatori di ricerca per profondita' (03/10/2026), SOLO con -DTRIUMV_SSTATS.
// Servono a confrontare le decisioni della nostra ricerca con quelle di Stockfish 19, che riceve una copia identica
// di questo file (scratchpad sfstats). Un thread, una posizione per processo: a fine processo, se la variabile
// d'ambiente SSTATS_FILE e' impostata, aggiunge al file una riga "chiave profondita' valore" per ogni contatore non
// nullo. La profondita' e' quella del nodo all'INGRESSO (prima di IIR, estensioni, depth-- interni).
// Senza TRIUMV_SSTATS le macro SSTAT/SSTATV non fanno nulla: codice generato identico (bench invariato).
// Script: match_moe/search_stats_vs_sf19.py (confronto con SF), match_moe/prune_grid.py (prove di opzioni).
#pragma once

#ifdef TRIUMV_SSTATS
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace SStats {
enum Key {
    NODES,      // ingressi nella ricerca principale (depth > 0)
    QNODES,     // ingressi in quiescenza
    LOOP,       // nodi che arrivano al ciclo delle mosse
    RAZOR, RFP, NMP_TRY, NMP_CUT, PROBCUT,
    LEGAL,      // mosse considerate nel ciclo (prima delle potature)
    MADE,       // mosse effettivamente giocate (dopo le potature del ciclo)
    MADE_Q, MADE_C, MADE_CHK, // ... di cui quiete (non scacco), catture/promozioni (non scacco), scacchi
    LMR_N,      // mosse cercate con il ramo LMR (dopo la prima)
    LMR_SUM,    // somma delle riduzioni positive applicate (ply)
    LMR_NEG,    // riduzioni negative (estensioni da LMR)
    LMR_RES,    // ri-ricerche a profondita' piena dopo un fail-high ridotto
    RED_Q, RED_C, RED_CHK,    // mosse effettivamente ridotte (riduzione > 0) per categoria
    PR_LMP, PR_FUT, PR_SEE, PR_HIST, PR_CAPFUT, // mosse potate nel ciclo, per regola
    SING_TRY, EXT1, EXT2, EXT3, EXTNEG, MULTICUT,
    FH,         // fail-high nel ciclo
    FH_FIRST,   // fail-high alla prima mossa giocata
    FH_MOVESUM, // somma del numero d'ordine della mossa che fa fail-high
    FH_HASTT,   // fail-high in nodi con una mossa TT
    FH_TT,      // fail-high fatti dalla mossa TT
    FH_CAP,     // fail-high fatti da una cattura/promozione
    FH_QUIET,   // fail-high fatti da una quieta
    // solo Triumviratus: perche' la futility quiet non scatta (quiete dalla 2a mossa in poi)
    QCONS, FUT_PV, FUT_INCHECK, FUT_DEEP, FUT_EVALUP, FUT_TRY, FUT_PDSUM, FUT_GAPSUM,
    TT_HIT, TT_MOVE,   // nodi principali (ply > 0) con entry TT trovata / con mossa TT
    FH_FIRST_CAP,      // fail-high fatti da una cattura giocata come prima mossa
    NKEYS
};
inline const char* const kName[NKEYS] = {
    "NODES", "QNODES", "LOOP", "RAZOR", "RFP", "NMP_TRY", "NMP_CUT", "PROBCUT", "LEGAL", "MADE", "MADE_Q", "MADE_C",
    "MADE_CHK", "LMR_N", "LMR_SUM", "LMR_NEG", "LMR_RES", "RED_Q", "RED_C", "RED_CHK", "PR_LMP", "PR_FUT", "PR_SEE",
    "PR_HIST", "PR_CAPFUT", "SING_TRY", "EXT1", "EXT2", "EXT3", "EXTNEG", "MULTICUT", "FH", "FH_FIRST",
    "FH_MOVESUM", "FH_HASTT", "FH_TT", "FH_CAP", "FH_QUIET",
    "QCONS", "FUT_PV", "FUT_INCHECK", "FUT_DEEP", "FUT_EVALUP", "FUT_TRY", "FUT_PDSUM", "FUT_GAPSUM",
    "TT_HIT", "TT_MOVE", "FH_FIRST_CAP"};
inline std::uint64_t c[NKEYS][64];
inline void add(int k, int d, std::int64_t v = 1) { c[k][d < 0 ? 0 : d > 63 ? 63 : d] += std::uint64_t(v); }
inline void dump() {
    const char* f = std::getenv("SSTATS_FILE");
    if (!f) return;
    if (std::FILE* o = std::fopen(f, "a")) {
        for (int k = 0; k < NKEYS; k++)
            for (int d = 0; d < 64; d++)
                if (c[k][d]) std::fprintf(o, "%s %d %llu\n", kName[k], d, (unsigned long long) c[k][d]);
        std::fclose(o);
    }
}
inline const int reg = (std::atexit(dump), 0);
}  // namespace SStats
#define SSTAT(k, d) SStats::add(SStats::k, (d))
#define SSTATV(k, d, v) SStats::add(SStats::k, (d), (v))
#else
#define SSTAT(k, d) ((void) 0)
#define SSTATV(k, d, v) ((void) 0)
#endif
