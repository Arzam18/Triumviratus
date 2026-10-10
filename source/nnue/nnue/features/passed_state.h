/*
  PassedState = PassedPawns v3 (Triumviratus, 10/10/2026; studio docs/audit_8.0/PASSER_BLOCK_STUDIO.md). Blocco da
  innesto bit 6 di nn_graft_mask (pawn_grafts.h: Offset 768, Dim 9600, hash "PST1"), nelle liste della pila come
  PassedRel (i due si escludono); recupero in search/06_nndirty.inc (nn_pst_step). Schizzo dell'agente dello studio,
  collegato e corretto il 10/10/2026 (re che cattura un pedone nel ramo degli eventi di pedone).

  Perche'. PassedRel (v2) si AGGIUNGE alla v1: ogni passato che compare, sparisce o avanza costa una riga v1 e una riga
  v2, e il suo bit "imprendibile" dipende dal re nemico, che nei finali muove in quasi meta' delle mosse (righe 1,0-1,4
  per valutazione nei finali, xperf PGO +2,88% di cicli). Qui il blocco SOSTITUISCE la v1: un passato = UNA riga, che
  contiene anche la riga v1 (la rete porta la v1 a zero; all'innesto ogni stato vale la riga v1, valutazione identica).
  Un passato che avanza costa quanto nella rete di partenza; righe in piu' solo quando lo STATO cambia, e lo stato e'
  scelto per cambiare di rado: nessun bit legato ai re salvo nei finali in cui l'avversario ha solo re e pedoni,
  nessuna "strada libera" sulle case lontane (solo la casa davanti).

  Una feature per pedone passato (stessa definizione di PassedPawns::passers), stato = stop + 5 * (prot + 2 * (conn + 2
  * mu)), 0..99:
    stop  la casa davanti: 0 vuota, 1 pezzo proprio, 2 cavallo o alfiere nemico, 3 torre o donna nemica, 4 re nemico;
    prot  un pedone proprio su una delle due case in diagonale dietro;
    conn  un altro passato dello stesso colore su una colonna adiacente;
    mu    pezzi dell'avversario del pedone: 1 solo cavalli/alfieri, 2 almeno una torre e nessuna donna, 3 almeno una
          donna; 0 / 4 solo re e pedoni, 4 = imprendibile (Chebyshev(re nemico, promozione) > mosse che mancano, 5
          dalla seconda traversa; come il bit 0 di PassedRel).
  Indice per prospettiva: stato * 96 + g, g = PassedPawns::make_index (indice della v1). 9600 righe int8 in coda alla
  tabella delle minacce, dopo i blocchi da innesto (fuori dalla permutazione per localita').
  Voce (per le liste della pila, uguale per le due prospettive, ordinata): colore << 13 | casa << 7 | stato.

  GPLv3, derived from Stockfish NNUE plumbing (see COPYING).
*/

#ifndef NNUE_FEATURES_PASSED_STATE_INCLUDED
#define NNUE_FEATURES_PASSED_STATE_INCLUDED

#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"
#include "feat_perm.h"
#include "full_threats.h"
#include "passed_pawns.h"

#include <cstdint>

namespace Triumviratus {
class NnBoard;
}

namespace Triumviratus::Eval::NNUE::Features {

// La rete porta PassedState e la sua v1 e' tutta a zero (controllato al caricamento, Network::read_parameters): allora la
// v1 non si calcola piu' (incrementale, ibrido, cache "pe" del refresh). Se la v1 non fosse a zero si lascia accesa:
// valutazione comunque esatta, solo piu' lenta.
inline bool V1Off = false;

class PassedState {
   public:
    // "PST1", deve coincidere con il trainer (model/modules/features/passed_state.py)
    static constexpr u32 HashValue = 0x50535431u;

    static constexpr int       Groups     = 96;
    static constexpr int       States     = 100;
    static constexpr IndexType Dimensions = Groups * States;  // 9600

    static constexpr IndexType MaxActiveDimensions = 16;
    using IndexList = FullThreats::IndexList;

    // Componenti dello stato e stato composto (stessa codifica del trainer)
    static constexpr int state(int stop, int prot, int conn, int mu) { return stop + 5 * (prot + 2 * (conn + 2 * mu)); }

    // Voce: colore << 13 | casa del motore << 7 | stato
    static constexpr std::uint16_t entry(int c, int sq, int st) { return std::uint16_t(c << 13 | sq << 7 | st); }
    static constexpr int entry_color(std::uint16_t e) { return e >> 13; }
    static constexpr int entry_sq(std::uint16_t e) { return (e >> 7) & 63; }
    static constexpr int entry_state(std::uint16_t e) { return e & 127; }

    // Riga (senza offset del blocco) della voce e per la prospettiva con il re in ksq.
    static inline IndexType make_index(Color perspective, int ksq, std::uint16_t e) {
        return IndexType(entry_state(e)) * Groups
             + PassedPawns::make_index(perspective, ksq, entry_color(e), entry_sq(e));
    }

    // Classe dei pezzi (non pedoni) di un colore dai suoi bitboard: 0 nessuno, 1 solo minori, 2 torri senza donne,
    // 3 donne.
    static constexpr int material_class(unsigned long long n, unsigned long long b, unsigned long long r,
                                        unsigned long long q) {
        return q ? 3 : r ? 2 : (n | b) ? 1 : 0;
    }

    // RIFERIMENTO (radice, vg_check_acc, -DTRIUMV_VERIFY_GRAFT): le voci della posizione, ordinate per colore e casa.
    // bb12 = bitboard per pezzo del motore (P N B R Q K p n b r q k: servono TUTTI), occ = tutti i pezzi.
    static int entries_of(const unsigned long long* bb12, unsigned long long occ, std::uint16_t out[16]);

    static void append_active_indices(Color perspective, const NnBoard& pos, IndexList& active);
};

}  // namespace Triumviratus::Eval::NNUE::Features

#endif  // #ifndef NNUE_FEATURES_PASSED_STATE_INCLUDED
