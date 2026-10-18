// Triumviratus, a UCI chess engine.
// Copyright (C) 2026 Francesco Torsello
// SPDX-License-Identifier: GPL-3.0-or-later (see COPYING)

// consilium/cn.h -- Consilium inference, written from the specification (block 6 of 8.1, 17/10/2026).
//
// Clean room: everything here comes from docs/audit_8.1/B6_SPECIFICA_CONSILIUM.md, which was derived from our trainer
// (serializer, quantization, data loader) and checked bit for bit against the previous inference used only as a
// black-box oracle. No line comes from the previous inference code (nnue/).
//
// The module reads the engine's board as it is (square a8 = 0, piece codes P N B R Q K p n b r q k = 0..11) and the
// per-move changes the engine's make writes on the stack (nn_dirty.h). It returns the two network outputs, psqt and
// positional, in internal units; what the engine does with them (nn_scale) stays in consilium_bridge.cpp.
//
// Since 18/10/2026 this is the engine's only inference; the previous one (nnue/nnue/) was removed.

#ifndef CN_H_INCLUDED
#define CN_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>

#include "../nn_dirty.h"

namespace cn {

// ---------------------------------------------------------------- network shape (specification, sec. 1-2)
constexpr int L1 = 1024, L1H = L1 / 2, L2 = 32, L3 = 32, NPSQT = 8, NBUCKETS = 8;
constexpr int ROWS_THR = 59808, ROWS_HKA = 4 * 32 * 704, ROWS_PP = 4560, ROWS_PAS = 96;
constexpr int HKA_PHASE_ROWS = 32 * 704;
constexpr int ROW16 = L1 + 32;   // int16 row: 2048 bytes of weights + 64 bytes (8 int32 PSQT + padding)
constexpr int ROW8  = L1 + 64;   // int8 row: 1024 bytes of weights + 64 bytes

// One output bucket. w1 is kept in two layouts: the file one (scalar path) and grouped by 4 inputs for the sparse
// SIMD product: w1g[g][o][k] = w1[o][4 l0_group(g) + k], in the order the SIMD forward stores l0 (cn.cpp).
struct alignas(64) Stack {
    std::int32_t b1[L2];
    std::int8_t  w1[L2][L1];
    std::int8_t  w1g[L1 / 4][L2][4];
    std::int32_t b2[L3];
    std::int8_t  w2[L3][2 * L2];
    std::int8_t  w2g[2 * L2 / 4][L3][4];   // w2g[g][o][k] = w2[o][4g + k]
    std::int32_t b3;
    std::int8_t  w3[2 * L2 + 2 * L3];
};

// All weights in one block of fixed size: it can be copied byte for byte (shared memory between processes).
struct alignas(64) Net {
    std::int16_t ftBias[L1];
    // Each feature row holds its 1024 weights and, right after them, its 8 PSQT values (int32), padded to a whole
    // number of 64-byte lines: the PSQT values arrive with the row instead of from a table of their own (that
    // second line per row was ~4% of the engine in the 17/10 profile).
    // (Widening the int8 blocks to int16 at load, one add per register instead of two, was measured on 17/10: +60%
    // L3 misses, -10% speed in the middlegame.)
    std::int16_t wHka[ROWS_HKA][ROW16];
    std::int8_t  wThr[ROWS_THR][ROW8];   // stored in the order of thr_slot (cn.cpp)
    std::int8_t  wPP[ROWS_PP][ROW8];
    std::int8_t  wPas[ROWS_PAS][ROW8];
    Stack        stack[NBUCKETS];
    std::uint64_t contentHash;   // FNV-1a of the file bytes, names the shared-memory copy
};

// Reads a .nnue file (or an image of one in memory) into `net`. On failure returns false with a reason in `err`.
bool load_file(const char* path, Net* net, std::string* err);
bool load_memory(const unsigned char* data, std::size_t size, Net* net, std::string* err);

// The engine's board (consilium_bridge.h): twelve piece bitboards P..k, occupancy white/black/all, mailbox (-1 = empty).
struct Board {
    const std::uint64_t* bb;
    const std::uint64_t* occ;
    const int*           mb;
    int                  stm;   // 0 white, 1 black
};

// Accumulator of one perspective: 1024 sums and the 8 PSQT sums.
struct alignas(64) Acc {
    std::int16_t v[L1];
    std::int32_t psqt[NPSQT];
};

// Refresh cache entry, one per (perspective, king square, phase): bias + HalfKA rows of the pieces it last saw, and
// those pieces. A refresh brings it to the board with only the pieces that changed, then adds the threat, pawn-pair
// and passed-pawn rows of the board.
struct alignas(64) KingCacheEntry {
    Acc           acc;
    std::uint64_t bb[12];
};

// Per-thread state: the dirty stack the engine writes, one accumulator per stack state and perspective (bit 1 of
// NnState::computed[p]), the refresh cache, room for the row lists of one evaluation.
struct alignas(64) Handle {
    NnStack       stack;
    Acc           acc[NN_STACK_SIZE][2];
    std::uint64_t cacheNet;           // contentHash of the network the cache was built from (0 = empty)
    KingCacheEntry    kcache[2][64][4];
    alignas(64) unsigned char scratch[4 * 50 * 1024];
};

struct Out {
    int psqt;
    int positional;
};

void init();                      // index tables (once, at startup)
void reset(Handle& h);            // back to the root: one state, nothing computed
Out  evaluate(const Net& net, Handle& h, const Board& b);       // incremental along the stack
Out  evaluate_refresh(const Net& net, const Board& b, Acc acc[2]);  // from scratch, both perspectives
bool same_accumulators(const Handle& h, const Acc acc[2]);       // nnueverify: top of the stack == refresh

extern int g_bucket_override;     // EvalBucketOverride probe: -1 normal, 0..7 forces the output bucket

}  // namespace cn

#endif  // CN_H_INCLUDED
