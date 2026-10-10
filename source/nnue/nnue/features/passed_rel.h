/*
  Pedoni passati con le loro relazioni (Triumviratus, PassedPawns v2, 09/10/2026; blocco opzionale da innestare).

  Perche'. Il blocco PassedPawns (v1) dice solo DOVE sta un passato. L'analisi del residuo del 09/10/2026 (punteggio a
  profondita' 1 contro ricerca a 300k nodi, 3.888 posizioni, tools/residual_features.py) dice che Consilium sottostima
  i passati soprattutto nei finali, e piu' quando il re nemico non li raggiunge o quando sono collegati: relazioni che
  v1 lascia alla rete. Questo blocco le da' esplicite.

  Una feature per ogni pedone passato (stessa definizione di PassedPawns::passers), con la casa e uno stato di 3 bit:
    bit 0  imprendibile: il re nemico e' fuori dal "quadrato" anche con il tratto (Chebyshev re-casa di promozione
           maggiore delle mosse che mancano al pedone; dalla seconda traversa il pedone ne fa due, quindi 5);
    bit 1  collegato: un altro passato dello stesso colore su una colonna adiacente;
    bit 2  strada libera: nessun pezzo sulle case davanti al pedone fino alla promozione.
  768 feature per prospettiva: (avversario ? 384 : 0) + stato * 48 + casa orientata - 8 (stessa orientazione di
  PassedPawns). Il riferimento del trainer deve dare gli stessi indici (vedi PASSED_REL_SPEC in testa al .cpp).

  Pesi int8 in coda all'array threatWeights dopo PassedPawns, FUORI dalla permutazione per localita' (feat_perm copre
  solo le prime FeatRows righe): riga = FoldOffset + indice. Blocco opzionale: una rete senza il blocco lo carica a
  zero e nn_graft_mask lo lascia spento, quindi nessun lavoro in piu' e valutazione identica.

  GPLv3, derived from Stockfish NNUE plumbing (see COPYING).
*/

#ifndef NNUE_FEATURES_PASSED_REL_INCLUDED
#define NNUE_FEATURES_PASSED_REL_INCLUDED

#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"
#include "feat_perm.h"
#include "full_threats.h"

#include <cstdint>
#ifdef TRIUMV_PREL_HIST
    #include <cstdio>
    #include <cstdlib>
#endif

namespace Triumviratus {
class NnBoard;
}

namespace Triumviratus::Eval::NNUE::Features {

// Formato "a base" (_wip graft_passedrel2, 10/10/2026; docs/audit_8.0/GRAFT_PASSEDREL_COSTO2.md §8): stessa rete,
// riparametrizzata senza riaddestrare. Per ogni gruppo g = (colore relativo, casa orientata), cioe' lo stesso indice
// della feature PassedPawns v1 ((c != prospettiva ? 48 : 0) + casa orientata - 8), uno stato base b(g) in 0..7 (8 =
// nessuna base): la riga W[g, b] si somma alla riga v1 del gruppo (che e' attiva esattamente quando c'e' il passato:
// stessa definizione), e le righe del blocco diventano W[g, s] - W[g, b], zero per s = b. Valutazione identica bit per
// bit (somme intere); la riga dello stato base non si applica piu' (filtro in pawn_grafts.h). Hash del blocco "PRB1".
// PrelBase vale solo con PrelBased; il caricamento lo scrive sempre (8 = nessuna base per i formati senza tabella).
inline bool          PrelBased = false;
inline std::uint8_t  PrelBase[96];
// _wip graft_passedrel3 (R1, 10/10/2026): il percorso caldo filtra con "stato != PrelBase[g]" senza leggere PrelBased:
// vale perche' senza formato a base la tabella e' tutta a 8 (FeatureTransformer::read_parameters la riempie di 8 prima
// di leggere, TRIUMV_PREL_NOFILTER la rimette a 8). Chi scrive PrelBase deve mantenere questa invariante.
// R3 (righe delta) ACCESO di default dal 10/10/2026 (utente; xperf PGO deterministico: finali -0,18 punti, mediogioco
// pari; valutazione identica). -DTRIUMV_PREL_NO_DELTA lo spegne (A/B: stessa macro sui due lati, le tabelle dei pesi si
// spostano di 1,15 MB).
#if !defined(TRIUMV_PREL_DELTA) && !defined(TRIUMV_PREL_NO_DELTA) && !defined(TRIUMV_NO_GRAFTS)  // senza blocchi: niente righe delta
    #define TRIUMV_PREL_DELTA
#endif
#ifdef TRIUMV_PREL_DELTA
// R3 (_wip graft_passedrel3): per gruppo, gli spigoli (bit 0-11, pawn_grafts.h prel_edge) la cui riga
// delta entra nell'int8 e si puo' usare. Scritta da FeatureTransformer::build_prel_delta a ogni rete letta.
inline std::uint16_t PrelDeltaOk[96];
#endif

#ifdef TRIUMV_PREL_HIST
// Diagnosi per scegliere gli stati base (§8.3): per gruppo g e stato s, quante righe di PassedRel la rete ha applicato
// (diff incrementali e ibride, refresh), sommate sulle prospettive. Lo stato base che toglie piu' righe e' l'argmax per
// gruppo. A fine processo, se PREL_HIST_FILE e' impostata, 96 righe "g c0 .. c7" in coda al file.
inline unsigned long long PrelHist[96][8];
inline void prel_hist_dump() {
    const char* f = std::getenv("PREL_HIST_FILE");
    if (!f) return;
    if (std::FILE* o = std::fopen(f, "a")) {
        for (int g = 0; g < 96; g++) {
            std::fprintf(o, "%d", g);
            for (int s = 0; s < 8; s++) std::fprintf(o, " %llu", PrelHist[g][s]);
            std::fprintf(o, "\n");
        }
        std::fprintf(o, "END\n");
        std::fclose(o);
    }
}
inline const int prel_hist_reg = (std::atexit(prel_hist_dump), 0);
    #define PREL_HIST(g, s) (++PrelHist[(g)][(s)])
#else
    #define PREL_HIST(g, s) ((void) 0)
#endif

class PassedRel {
   public:
    // "PRV2", deve coincidere con il trainer
    static constexpr u32 HashValue = 0x50525632u;
    // "PRB1": stesso blocco nel formato a base (tabella di 96 byte dopo le righe e la PSQT del blocco)
    static constexpr u32 HashValueBased = 0x50524231u;

    static constexpr IndexType Dimensions = 768;

    // Righe dopo FullThreats + PawnPair + PassedPawns (FeatRows), fuori dalla permutazione per localita'.
    static constexpr IndexType FoldOffset = FeatRows;

    // Al massimo 16 passati.
    static constexpr IndexType MaxActiveDimensions = 16;
    using IndexList = FullThreats::IndexList;
    // 09/10/2026 sera: l'aggiornamento incrementale passa dal meccanismo comune dei blocchi da innesto (pawn_grafts.h).

    // pc = colore del pedone, state = 3 bit (vedi sopra), sq = casa del motore (a8 = 0).
    static inline IndexType make_index(Color perspective, int ksq, int pc, int state, int sq) {
        const int orientation = FullThreats::OrientTBL[ksq] ^ (56 * (1 - int(perspective)));
        return (pc != int(perspective) ? 384 : 0) + state * 48 + (sq ^ orientation) - 8;
    }

    // Le voci di una posizione: (colore << 9) | (stato << 6) | casa, ordinate per colore e casa. bb12 = bitboard per
    // pezzo del motore (P N B R Q K p n b r q k; servono pedoni e re), occ = tutti i pezzi. Restituisce il numero.
    static int entries_of(const unsigned long long* bb12, unsigned long long occ, std::uint16_t out[16]);

    static void append_active_indices(Color perspective, const NnBoard& pos, IndexList& active);
};

}  // namespace Triumviratus::Eval::NNUE::Features

#endif  // #ifndef NNUE_FEATURES_PASSED_REL_INCLUDED
