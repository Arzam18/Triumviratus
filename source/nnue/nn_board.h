// La scacchiera del motore vista dalla rete (07/10/2026, scacchiera unica v2).
//
// La rete non ha una scacchiera sua e non traduce nulla: legge quella della ricerca cosi' com'e', nella sua numerazione
// (case a8 = 0 .. h1 = 63; bitboard per pezzo nell'ordine P N B R Q K p n b r q k; occupazioni bianco/nero/tutte;
// mailbox con -1 = casa vuota). Le tabelle degli indici delle feature sono costruite per questa numerazione
// (features/*.h). Nessun dato della posizione vive qui: solo i puntatori e il lato al tratto.

#ifndef NN_BOARD_H_INCLUDED
#define NN_BOARD_H_INCLUDED

#include <cstdint>

#include "bitboard.h"
#include "types.h"

namespace Triumviratus {

class NnBoard {
   public:
    NnBoard(const unsigned long long* bb, const unsigned long long* occ, const int* mailbox, Color stm) :
        bb_(bb),
        occ_(occ),
        mb_(mailbox),
        stm_(stm) {}

    NnBoard(const NnBoard&)            = delete;
    NnBoard& operator=(const NnBoard&) = delete;

    Bitboard                  occ() const { return occ_[2]; }
    Bitboard                  occ(Color c) const { return occ_[c]; }
    Bitboard                  bb(int pc) const { return bb_[pc]; }  // codice del motore 0..11
    const unsigned long long* bbs() const { return bb_; }           // i dodici bitboard per pezzo
    Bitboard        type(int t) const { return bb_[t] | bb_[t + NN_BLACK]; }  // NN_PAWN..NN_KING, entrambi i colori
    Bitboard        pawns(Color c) const { return bb_[NN_PAWN + NN_BLACK * c]; }
    int             piece_on(int s) const { return mb_[s]; }  // codice del motore, -1 = vuota
    Square          king(Color c) const { return Square(lsb(bb_[NN_KING + NN_BLACK * c])); }
    int             count() const { return popcount(occ_[2]); }
    int             count_type(int t) const { return popcount(type(t)); }
    Color           side_to_move() const { return stm_; }

   private:
    const unsigned long long* bb_;
    const unsigned long long* occ_;
    const int*                mb_;
    Color                     stm_;
};

}  // namespace Triumviratus

#endif  // #ifndef NN_BOARD_H_INCLUDED
