/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Class for difference calculation of NNUE evaluation function

#ifndef NNUE_ACCUMULATOR_H_INCLUDED
#define NNUE_ACCUMULATOR_H_INCLUDED

#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

#include "../types.h"
#include "../misc.h"
#include "nnue_architecture.h"
#include "nnue_common.h"

namespace Triumviratus {
class NnBoard;
}

namespace Triumviratus::Eval::NNUE {

struct alignas(CacheLineSize) Accumulator;

class FeatureTransformer;

// Class that holds the result of affine transformation of input features,
// combined HalfKA + Threats. Se e' calcolato (per prospettiva) lo dice lo stato della stessa mossa (NnState::computed,
// ../../nn_dirty.h), che la make del motore azzera quando lo crea.
struct alignas(CacheLineSize) Accumulator {
    std::array<std::array<i16, L1>, COLOR_NB>          accumulation;
    std::array<std::array<i32, PSQTBuckets>, COLOR_NB> psqtAccumulation;
};


// AccumulatorCaches struct provides per-thread accumulator caches, where each
// cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea, was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCaches {
    template<typename Network>
    AccumulatorCaches(const Network& network) {
        clear(network);
    }

    // 07/10/2026 (scacchiera unica v2): la posizione di una entry sono i dodici bitboard per pezzo del motore (codici
    // 0..11, case a8 = 0), confrontati pezzo per pezzo con quelli della scacchiera. Prima era la mailbox in forma rete
    // (64 byte) piu' l'occupazione.
    struct alignas(CacheLineSize) Entry {
        std::array<BiasType, L1>                accumulation;
        std::array<PSQTWeightType, PSQTBuckets> psqtAccumulation;
        std::array<Bitboard, 12>                pieces;

        // To initialize a refresh entry, we set all its bitboards empty,
        // so we put the biases in the accumulation, without any weights on top
        void clear(const std::array<BiasType, L1>& biases) {
            accumulation = biases;
            std::memset(reinterpret_cast<std::byte*>(this) + offsetof(Entry, psqtAccumulation), 0,
                        sizeof(Entry) - offsetof(Entry, psqtAccumulation));
        }
    };

    template<typename Network>
    void clear(const Network& network) {
        for (auto& perPhase : entries)
            for (auto& entries1D : perPhase)
                for (auto& entry : entries1D)
                    entry.clear(network.featureTransformer.biases);
    }

    // Una tabella per fascia di materiale (HalfKA a esperti, TRIUMV_PSQ_PHASES > 1): l'accumulazione di una entry
    // vale solo per i pesi della sua fascia. Con 1 fascia e' la tabella di sempre.
    std::array<Entry, COLOR_NB>& at(int phase, Square sq) { return entries[phase][sq]; }
    std::array<Entry, COLOR_NB>& operator[](Square sq) { return entries[0][sq]; }

    std::array<std::array<std::array<Entry, COLOR_NB>, SQUARE_NB>, TRIUMV_PSQ_PHASES> entries;
};


// Catena degli accumulatori di un thread (07/10/2026, scacchiera unica v2). Le dirty di ogni mossa e i flag "calcolato"
// stanno in `ds` (NnStack, ../../nn_dirty.h): li scrive la make del motore, che vi aggiunge uno stato per mossa e lo
// toglie alla unmake (nnue_bridge.h: nn_pos_stack). La rete legge le dirty, calcola gli accumulatori e segna i flag.
class AccumulatorStack {
   public:
    static constexpr usize MaxSize = NN_STACK_SIZE;
    static_assert(MaxSize == MAX_PLY + 1, "NN_STACK_SIZE deve essere MAX_PLY + 1");

    [[nodiscard]] const Accumulator& latest() const noexcept;

    // Riparte da una radice da calcolare: un solo stato, nessun accumulatore calcolato.
    void reset() noexcept;
    NnStack& dirty_stack() noexcept { return ds; }

    void evaluate(const NnBoard&            pos,
                  const FeatureTransformer& featureTransformer,
                  // Silence spurious warning on GCC 10
                  [[maybe_unused]] AccumulatorCaches& cache) noexcept;

    // Triumviratus 05/10/2026: le otto liste di indici dell'aggiornamento a due prospettive
    // (update_accumulator_incremental_both) stanno qui, una copia per thread, invece che sullo stack. Le quattro
    // liste threat da 1,2 KB portavano il frame di evaluate oltre i 4 KB, e su Windows ogni valutazione passava
    // dalla sonda dello stack (__chkstk). Si svuotano a ogni uso: contenuto e ordine identici.
    // xperf 6 giri (release PGO avx512): da sola cicli +0,09% (rumore), insieme alla coda delle mosse fuori da
    // queue_next (11_queue.inc) -1,55% contro -0,81% di quella sola.
    struct BothLists {
        PSQFeatureSet::IndexList    psqRemW, psqAddW, psqRemB, psqAddB;
        ThreatFeatureSet::IndexList thrRemW, thrAddW, thrRemB, thrAddB;
    };

   private:
    [[nodiscard]] Accumulator& mut_latest() noexcept;
    [[nodiscard]] usize        size() const noexcept { return usize(ds.size); }

    void evaluate_side(Color                     perspective,
                       const NnBoard&            pos,
                       const FeatureTransformer& featureTransformer,
                       // Silence spurious warning on GCC 10
                       [[maybe_unused]] AccumulatorCaches& cache) noexcept;

    [[nodiscard]] usize find_last_usable_accumulator(Color perspective) const noexcept;

    void forward_update_incremental(Color                     perspective,
                                    const NnBoard&            pos,
                                    const FeatureTransformer& featureTransformer,
                                    const usize               begin) noexcept;

    void backward_update_incremental(Color                     perspective,
                                     const NnBoard&            pos,
                                     const FeatureTransformer& featureTransformer,
                                     const usize               end) noexcept;

    std::array<Accumulator, MaxSize> accumulators;
    BothLists                        both_lists;
    NnStack                          ds;
};

}  // namespace Triumviratus::Eval::NNUE

#endif  // NNUE_ACCUMULATOR_H_INCLUDED
