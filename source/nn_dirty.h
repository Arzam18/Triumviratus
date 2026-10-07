// nn_dirty.h -- Cio' che una mossa cambia per la rete, scritto dalla make del motore (07/10/2026, scacchiera unica v2).
//
// Una sola numerazione: case del MOTORE (a8 = 0 .. h1 = 63, 64 = nessuna casa) e codici del motore (0..11 = P N B R Q K
// p n b r q k). La make scrive qui il pezzo mosso (NnDirtyPiece), su una pila con uno stato per mossa (NnStack); i
// pedoni toccati (NnDirtyPawns) e le tuple di minaccia che cambiano (NnDirtyThreats) li aggiunge il motore prima di una
// valutazione (search/06_nndirty.inc, nn_dirty_catch_up). La rete (nnue/) le legge cosi' come sono. Le tabelle degli
// indici delle feature lavorano direttamente in questa numerazione (vedi nnue/nnue/features/*.h): nessuna conversione
// a runtime.
//
// Il file e' incluso dal motore (threads.cpp, attraverso nnue_bridge.h) e dalla rete (nnue/types.h): solo tipi semplici,
// nessun tipo ne' macro dell'uno o dell'altra. Lo stesso layout in ogni unita' di compilazione: nessun campo dipende
// da macro.

#ifndef NN_DIRTY_H_INCLUDED
#define NN_DIRTY_H_INCLUDED

#include <cstdint>

constexpr int NN_SQ_NONE = 64;  // "nessuna casa" (uguale a no_sq del motore)

// Tipi di pezzo del motore: codice % 6. Il nero e' il bianco + 6.
enum : int { NN_PAWN = 0, NN_KNIGHT = 1, NN_BISHOP = 2, NN_ROOK = 3, NN_QUEEN = 4, NN_KING = 5, NN_BLACK = 6 };

// Pila delle dirty: uno stato per ogni mossa della ricerca dalla radice (le mosse nulle non aggiungono stati: la
// scacchiera non cambia). 247 = MAX_PLY (246) della rete + la radice; la ricerca non supera max_ply + 16 semimosse.
constexpr int NN_STACK_SIZE = 247;

struct NnDirtyPiece {
    std::uint8_t pc;          // pezzo che muove
    std::uint8_t from, to;    // to = NN_SQ_NONE per le promozioni (il pezzo che arriva e' add_pc)
    std::uint8_t remove_pc;   // pezzo catturato, o torre dell'arrocco che parte
    std::uint8_t remove_sq;   // sua casa (en passant: la casa del pedone, non `to`); NN_SQ_NONE se nessuno
    std::uint8_t add_pc;      // pezzo promosso, o torre dell'arrocco che arriva
    std::uint8_t add_sq;      // sua casa; NN_SQ_NONE se nessuno. Arrocco = to e add_sq entrambi presenti.
    std::uint8_t phase;       // fascia di materiale (HalfKA a esperti) della posizione DOPO la mossa
    std::uint8_t phaseChanged;  // 1 se una cattura ha cambiato fascia: refresh dell'accumulatore, come per il re
};

// Una tupla di minaccia in 32 bit: casa dell'attaccante (0-7), casa dell'attaccato (8-15), pezzo attaccato (16-19),
// pezzo attaccante (20-23), 1 = la minaccia compare / 0 = sparisce (31).
constexpr int NN_THR_PCSQ = 0, NN_THR_TSQ = 8, NN_THR_TPC = 16, NN_THR_PC = 20;
inline std::uint32_t nn_threat(int pc, int tpc, int pcSq, int tSq, bool add) {
    return (std::uint32_t(add) << 31) | (std::uint32_t(pc) << NN_THR_PC) | (std::uint32_t(tpc) << NN_THR_TPC)
         | (std::uint32_t(tSq) << NN_THR_TSQ) | std::uint32_t(pcSq);
}

// Un pezzo partecipa al massimo a 8 minacce in uscita e 16 in entrata, e muovendosi ne scopre al massimo 8: una mossa
// che non e' un arrocco ne cambia al massimo (8 + 16) * 3 + 8 = 80, un arrocco 36. Le 16 voci in piu' accolgono le
// scritture vettoriali da 16 tuple oltre il conteggio (emissione AVX-512).
constexpr int NN_THREATS_MAX = 96;
struct NnDirtyThreats {
    std::uint32_t n;
    std::uint32_t list[NN_THREATS_MAX];
};

// I pedoni che una mossa toglie (al massimo 2: pedone per pedone, en passant) e aggiunge (al massimo 1; una promozione
// nessuno), con i bitboard dei pedoni PRIMA della mossa, per i blocchi PawnPair e PassedPawns.
struct NnDirtyPawns {
    std::uint64_t before[2];      // pedoni bianchi, neri prima della mossa
    std::uint8_t  removedSq[2], removedC[2];
    std::uint8_t  addedSq, addedC;  // addedSq = NN_SQ_NONE se nessuno
    std::uint8_t  nRemoved;
    std::uint8_t  any;            // 0 = la mossa non tocca pedoni: niente da fare a valle
};

struct NnState {
    NnDirtyPiece  dp;
    std::uint8_t  computed[2];    // accumulatore di questo stato calcolato, per prospettiva (bianco, nero)
    NnDirtyPawns  pawns;
    std::uint64_t key;           // -DTRIUMV_VERIFY_NNSYNC: chiave dei pezzi dopo la mossa (controllo di sincronia)
    NnDirtyThreats threats;
};

struct NnStack {
    int     size;   // stati presenti; lo stato 0 e' la radice
    int     ready;  // gli stati [ready, size) non hanno ancora minacce e pedoni (nn_dirty_catch_up)
    NnState st[NN_STACK_SIZE];
};

#endif  // NN_DIRTY_H_INCLUDED
