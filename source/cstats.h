// cstats.h -- precisione delle correzioni della valutazione (06/10/2026, rapporto docs/audit_8.0/N_CORREZIONI.md 4.1),
// SOLO con -DTRIUMV_CORRSTATS. Nel punto in cui la ricerca impara le correzioni (fine nodo, 13_search.inc) si
// accumulano, per esperto della rete x fascia del contatore delle 50 mosse x tipo di limite, i residui
//   r0 = best - eval della rete (senza correzione)   e   r1 = best - eval corretta:
// conteggio, somma, somma dei quadrati. Da qui: quanta varianza dell'errore spiegano le correzioni (1 - q1/q0) e il
// residuo medio per esperto e per fascia (se e' ~0 in ogni fascia, una tabella per esperto non ha niente da imparare).
// Il numero assoluto e' distorto dai limiti (residuo censurato); il confronto fra varianti sullo stesso insieme no.
// A fine processo, se la variabile d'ambiente CSTATS_FILE e' impostata, aggiunge al file una riga per cella non vuota:
// "esperto fascia limite n s0 q0 s1 q1". Script: match_moe/corrstats.py.
// Senza TRIUMV_CORRSTATS la macro CSTAT non fa nulla: codice generato identico (bench invariato).
#pragma once

#ifdef TRIUMV_CORRSTATS
#include <cstdio>
#include <cstdlib>

namespace CStats {
inline double n[4][3][3], s0[4][3][3], q0[4][3][3], s1[4][3][3], q1[4][3][3];   // [esperto][fascia 50][limite]
inline void add(int ph, int fb, int kind, int r0, int r1) {
    n[ph][fb][kind] += 1;
    s0[ph][fb][kind] += r0;
    q0[ph][fb][kind] += double(r0) * r0;
    s1[ph][fb][kind] += r1;
    q1[ph][fb][kind] += double(r1) * r1;
}
inline void dump() {
    const char* f = std::getenv("CSTATS_FILE");
    if (!f) return;
    if (std::FILE* o = std::fopen(f, "a")) {
        for (int p = 0; p < 4; p++)
            for (int b = 0; b < 3; b++)
                for (int k = 0; k < 3; k++)
                    if (n[p][b][k])
                        std::fprintf(o, "%d %d %d %.0f %.0f %.0f %.0f %.0f\n", p, b, k, n[p][b][k], s0[p][b][k],
                                     q0[p][b][k], s1[p][b][k], q1[p][b][k]);
        std::fclose(o);
    }
}
inline const int reg = (std::atexit(dump), 0);
}  // namespace CStats
#define CSTAT(ph, fb, kind, r0, r1) CStats::add((ph), (fb), (kind), (r0), (r1))
#else
#define CSTAT(ph, fb, kind, r0, r1) ((void) 0)
#endif
