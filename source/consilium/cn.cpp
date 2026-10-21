// Triumviratus, a UCI chess engine.
// Copyright (C) 2026 Francesco Torsello
// Copyright (C) 2026 A Tin Man (interleaved int8 rows of the first layer, CN_W1)
// SPDX-License-Identifier: GPL-3.0-or-later (see COPYING)

// consilium/cn.cpp -- Consilium inference (block 6 of 8.1). See cn.h for the clean-room rule.
// Section numbers ("spec 3.2") refer to docs/audit_8.1/B6_SPECIFICA_CONSILIUM.md.

#include "cn.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>
#include <new>
#include <vector>

#include "../nn_attacks.h"

#if defined(USE_AVX2) || defined(USE_AVX512)
    #include <immintrin.h>
#endif

extern unsigned long long pawn_attacks[2][64];   // engine (attacks.cpp): [colour][square], a8 = 0

#if defined(__GNUC__) || defined(__clang__)
    #define CN_POPCNT(b) __builtin_popcountll(b)
    #define CN_LSB(b) __builtin_ctzll(b)
#else
    #include <intrin.h>
    #define CN_POPCNT(b) int(__popcnt64(b))
static inline int CN_LSB(unsigned long long b) { unsigned long i; _BitScanForward64(&i, b); return int(i); }
#endif

// Prefetch of one cache line of a weight row (-DCN_NO_PREFETCH: none).
#if defined(CN_NO_PREFETCH)
    #define CN_PREFETCH(p) ((void)0)
#elif defined(USE_AVX2) || defined(USE_AVX512)
    #define CN_PREFETCH(p) _mm_prefetch(reinterpret_cast<const char*>(p), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
    #define CN_PREFETCH(p) __builtin_prefetch(p)
#else
    #define CN_PREFETCH(p) ((void)0)
#endif

// CN_W1 (block 8, 18/10/2026; the idea is A Tin Man's, 15/10/2026, measured on the previous inference): the int8 rows
// (threats, pawn pairs, passed pawns) are stored with their bytes interleaved in blocks of 32 (byte 2j = element j,
// byte 2j + 1 = element 16 + j). One 32-byte load then gives two int16 vectors: vpmaddubsw with (1, 0) pairs keeps the
// even bytes, vpsraw by 8 the odd ones, both on ports 0/1, instead of two vpmovsxbw on port 5 (the bottleneck of the
// threat rows). Same values, same result. His measurements, identical nodes: Zen+ (avx2-nopext) +4.49%, Raptor Lake
// (avxvnni) +1.09%, Zen 3 (avx2) -2.15%: so only in the AVX2 builds without PEXT or with AVX-VNNI. -DCN_NO_W1: off.
// Consilium-2 (21/10/2026): the HalfKA rows are int8 as well, so almost every row of a move is widened, and the
// balance measured on Consilium may not hold. AVX-512 uses blocks of 64 bytes (byte 2j = element j, byte 2j + 1 =
// element 32 + j): one 64-byte load, two registers of 32 values. xperf on the Xeon (Skylake-SP, PGO, ep25, 4 rounds):
// -1.91% cycles per node in the middlegame (IPC 1.51 -> 1.58), -1.98% in endgames. On Zen 4 (Ryzen 7 8845HS, vnni512,
// PGO, same day) it costs instead: +2.28% in the middlegame, +11.7% in endgames. So on by default only in the AVX-512
// build without VNNI of Consilium-2 (Skylake-SP/X); off in vnni512 and avx512icl (Zen 4/5, Ice Lake and later; not
// measured on Intel with VNNI). -DCN_FORCE_W1 turns it on in any AVX2 or AVX-512 build, to be measured per processor.
#if !defined(CN_NO_W1) && ((defined(CN_FORCE_W1) && (defined(USE_AVX2) || defined(USE_AVX512))) \
                           || (defined(USE_AVX512) && !defined(USE_VNNI) && defined(TRIUMV_C2)) \
                           || (defined(USE_AVX2) && !defined(USE_AVX512) && (!defined(USE_PEXT) || defined(USE_AVXVNNI))))
    #define CN_W1 1
    #if defined(USE_AVX512)
        #define CN_W1_BLOCK 64
    #else
        #define CN_W1_BLOCK 32
    #endif
#endif

// Independent chains of the sparse first layer: three when it uses vpdpbusd (latency about five cycles, AVX-512 VNNI
// or AVX-VNNI), two otherwise. -DCN_DOT_CHAINS=2 goes back to two (not measured on a VNNI processor here).
#ifndef CN_DOT_CHAINS
    #if defined(USE_VNNI)
        #define CN_DOT_CHAINS 3
    #else
        #define CN_DOT_CHAINS 2
    #endif
#endif


// ---------------------------------------------------------------- -DCN_PROF: cycles per phase (diagnostic only)
#ifdef CN_PROF
    #include <x86intrin.h>
struct CnProf {
    unsigned long long cyc[8] = {}, cnt[8] = {};
    ~CnProf() {
        static const char* name[8] = {"evaluate", "refreshH", "gatherO", "apply", "forward", "pkcompute", "nnz4", "chain"};
        for (int i = 0; i < 8; i++)
            if (cnt[i])
                std::fprintf(stderr, "cnprof %-9s calls %12llu  cycles/call %8.1f  total Mcyc %10.1f\n", name[i], cnt[i],
                             double(cyc[i]) / cnt[i], cyc[i] / 1e6);
    }
};
static CnProf g_prof;
struct CnTimer {
    int i; unsigned long long t0;
    explicit CnTimer(int k) : i(k), t0(__rdtsc()) {}
    ~CnTimer() { g_prof.cyc[i] += __rdtsc() - t0; g_prof.cnt[i]++; }
};
    #define CN_TIME(k) CnTimer cn_timer_##k(k)
    #define CN_COUNT(k, v) (g_prof.cnt[k]++, g_prof.cyc[k] += (v))
#else
    #define CN_TIME(k)
    #define CN_COUNT(k, v)
#endif

namespace cn {

int g_bucket_override = -1;
// Expert per piece count. Default = the bands of the network (<= 9, 10-15, 16-23, >= 24, as PHASE_OF_COUNT below);
// ExpertFix replaces it at the start of a search (consilium_bridge nn_set_phase_map).
unsigned char g_phase_map[33] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2,
                                 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};
#ifdef TRIUMV_C2
std::uint8_t g_router[ROUTER_CELLS];
#endif

namespace {

using u64 = std::uint64_t;

// ================================================================ index tables (spec 3)
//
// Input squares are the engine's (a8 = 0). The specification numbers squares as the trainer (a1 = 0): trainer square
// = engine square ^ 56. Tables are built once in trainer terms and indexed by engine squares.

constexpr int T(int s) { return s ^ 56; }
constexpr int PHASE_OF_COUNT[33] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2,
                                    2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};

// Orientation (spec 3): vertical flip for black, file mirror by the king's file. HalfKA mirrors when the king is on
// files a-d, the other three blocks when it is on files e-h.
constexpr int orient(int persp, bool mirror) { return (persp ? 56 : 0) ^ (mirror ? 7 : 0); }
constexpr bool mirror_hka(int ksq) { return (ksq & 7) < 4; }   // same file in both numberings
constexpr bool mirror_oth(int ksq) { return (ksq & 7) >= 4; }

std::int32_t g_kb[2][64];            // [persp][ksq]: king bucket * 704
std::int16_t g_hk[2][2][12][64];     // [persp][mirror][piece][sq]: 64 * type11 + oriented square
std::int32_t g_thrA[10][64];         // [attacker x][from']: base of the attacker + offset of its square
std::int32_t g_thrS[10][2][6];       // [x][target colour][target type]: slot * squares of x, -1 = no feature
std::uint8_t g_thrL[10][64][64];     // [x][from'][to']: empty-board attacks of x from from' below to'
std::int8_t  g_pid[2][2][2][64];     // [persp][mirror][colour][sq]: pawn id 0..95 (PP_3Wide, PassedPawns), -1 off
u64          g_ppMask[64];           // pawns paired with a pawn on sq: same or adjacent files, ranks 2-7, not sq
u64          g_frontFile[2][64];     // squares ahead on the same file, for colour c (engine numbering)
u64          g_frontSpan[2][64];     // squares ahead on the same and adjacent files

constexpr int PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5;

// Trainer numbering helpers (a1 = 0).
u64 t_pawn_attacks(int s, int c) {
    const u64 b = 1ULL << s, notA = ~0x0101010101010101ULL, notH = ~0x8080808080808080ULL;
    return c == 0 ? ((b & notH) << 9) | ((b & notA) << 7) : ((b & notA) >> 9) | ((b & notH) >> 7);
}
u64 t_empty_attacks(int pt, int s) {
    static const int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    const int f0 = s & 7, r0 = s >> 3;
    u64 out = 0;
    if (pt == KNIGHT) {
        for (auto& d : kn) {
            const int f = f0 + d[0], r = r0 + d[1];
            if (f >= 0 && f < 8 && r >= 0 && r < 8) out |= 1ULL << (r * 8 + f);
        }
        return out;
    }
    static const int dirs[8][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const int first = pt == ROOK ? 4 : 0, last = pt == BISHOP ? 4 : 8;
    for (int k = first; k < last; k++)
        for (int f = f0 + dirs[k][0], r = r0 + dirs[k][1]; f >= 0 && f < 8 && r >= 0 && r < 8;
             f += dirs[k][0], r += dirs[k][1])
            out |= 1ULL << (r * 8 + f);
    return out;
}

// Threat slot of (attacker type, target type), spec 3.2.
constexpr int THREAT_SLOT[5][6] = {
    {-1, 0, -1, 1, -1, -1},  // pawn: knight, rook
    {0, 1, 2, 3, 4, -1},     // knight
    {0, 1, 2, 3, -1, -1},    // bishop
    {0, 1, 2, 3, -1, -1},    // rook
    {0, 1, 2, 3, 4, -1},     // queen
};
constexpr int NUM_VALID[5] = {4, 10, 8, 8, 10};

void build_tables() {
    // HalfKA (spec 3.1). King bucket of the oriented king square (files e-h only): 31 - 4*rank - (file - 4).
    for (int p = 0; p < 2; p++)
        for (int ksq = 0; ksq < 64; ksq++) {
            const int ko = T(ksq) ^ orient(p, mirror_hka(ksq));
            g_kb[p][ksq] = (31 - 4 * (ko >> 3) - ((ko & 7) - 4)) * 704;
        }
    for (int p = 0; p < 2; p++)
        for (int m = 0; m < 2; m++)
            for (int pc = 0; pc < 12; pc++)
                for (int s = 0; s < 64; s++) {
                    const int type = pc % 6, colour = pc / 6;
                    const int t11 = type == KING ? 10 : 2 * type + (colour != p);
                    g_hk[p][m][pc][s] = std::int16_t(t11 * 64 + (T(s) ^ orient(p, m)));
                }

    // Full_Threats (spec 3.2), trainer numbering, attackers x = colour * 5 + type.
    int base = 0;
    for (int x = 0; x < 10; x++) {
        const int c = x / 5, pt = x % 5;
        int off = 0;
        for (int f = 0; f < 64; f++) {
            g_thrA[x][f] = off;
            const u64 att = pt == PAWN ? ((f >= 8 && f <= 55) ? t_pawn_attacks(f, c) : 0) : t_empty_attacks(pt, f);
            for (int t = 0; t < 64; t++)
                g_thrL[x][f][t] = std::uint8_t(CN_POPCNT(att & ((1ULL << t) - 1)));
            off += CN_POPCNT(att);
        }
        const int tot = off;
        for (int f = 0; f < 64; f++)
            g_thrA[x][f] += base;
        for (int dc = 0; dc < 2; dc++)
            for (int dt = 0; dt < 6; dt++) {
                const int slot = THREAT_SLOT[pt][dt];
                g_thrS[x][dc][dt] = slot < 0 ? -1 : (dc * (NUM_VALID[pt] / 2) + slot) * tot;
            }
        base += NUM_VALID[pt] * tot;
    }
    if (base != ROWS_THR)
        std::fprintf(stderr, "cn: threat table size %d != %d\n", base, ROWS_THR);

    // Pawn ids (spec 3.3, 3.4).
    for (int p = 0; p < 2; p++)
        for (int m = 0; m < 2; m++)
            for (int c = 0; c < 2; c++)
                for (int s = 0; s < 64; s++) {
                    const int o = T(s) ^ orient(p, m);
                    g_pid[p][m][c][s] = (o < 8 || o > 55) ? -1 : std::int8_t(48 * (c ^ p) + o - 8);
                }
    for (int s = 0; s < 64; s++) {
        const int f = s & 7, r = s >> 3;   // engine row: 0 = rank 8
        u64 files = 0x0101010101010101ULL << f;
        if (f > 0) files |= 0x0101010101010101ULL << (f - 1);
        if (f < 7) files |= 0x0101010101010101ULL << (f + 1);
        g_ppMask[s] = files & ~0xFFULL & ~(0xFFULL << 56) & ~(1ULL << s);
        u64 aheadW = 0, aheadB = 0;   // white advances towards row 0, black towards row 7
        for (int rr = 0; rr < 8; rr++)
            (rr < r ? aheadW : rr > r ? aheadB : aheadW) |= rr == r ? 0 : 0xFFULL << (8 * rr);
        g_frontFile[0][s] = aheadW & (0x0101010101010101ULL << f);
        g_frontFile[1][s] = aheadB & (0x0101010101010101ULL << f);
        g_frontSpan[0][s] = aheadW & files;
        g_frontSpan[1][s] = aheadB & files;
    }
}

inline int threat_index(int p, int o, int apc, int tpc, int fe, int te) {
    const int at = apc % 6, ac = (apc >= 6) ^ p;
    const int dt = tpc % 6, dc = (tpc >= 6) ^ p;
    if (at == KING) return -1;
    const int x = ac * 5 + at;
    const int sl = g_thrS[x][dc][dt];
    if (sl < 0) return -1;
    const int f = T(fe) ^ o, t = T(te) ^ o;
    if (at == dt && (ac != dc || at != PAWN) && f < t) return -1;
    return g_thrA[x][f] + sl + g_thrL[x][f][t];
}

// The same index from precomputed tables (17/10, the counters showed ~50 instructions per tuple and perspective):
// base of (perspective, half, attacker, target, from) and the attack-count part of (perspective, half, attacker,
// from, to); same-type pairs are kept once (spec 3.2) through g_same and the oriented order g_lt. Checked against
// threat_index on every combination at startup.
std::int32_t g_tb[2][2][12][12][64];
std::uint8_t g_to[2][2][12][64][64];
std::uint8_t g_lt[2][2][64][64];
std::uint8_t g_same[12][12];

void build_fast_threats() {
    for (int a = 0; a < 12; a++)
        for (int d = 0; d < 12; d++) {
            const bool enemy = (a >= 6) != (d >= 6);
            g_same[a][d] = std::uint8_t(a % 6 == d % 6 && (enemy || a % 6 != PAWN));
        }
    for (int p = 0; p < 2; p++)
        for (int m = 0; m < 2; m++) {
            const int o = orient(p, m);
            for (int f = 0; f < 64; f++)
                for (int t = 0; t < 64; t++) g_lt[p][m][f][t] = std::uint8_t((T(f) ^ o) < (T(t) ^ o));
            for (int a = 0; a < 12; a++) {
                const int at = a % 6, ac = (a >= 6) ^ p;
                for (int f = 0; f < 64; f++) {
                    const int fo = T(f) ^ o;
                    for (int d = 0; d < 12; d++) {
                        const int dt = d % 6, dc = (d >= 6) ^ p;
                        int base = -1;
                        if (at != KING) {
                            const int x = ac * 5 + at, sl = g_thrS[x][dc][dt];
                            if (sl >= 0) base = g_thrA[x][fo] + sl;
                        }
                        g_tb[p][m][a][d][f] = base;
                    }
                    for (int t = 0; t < 64; t++)
                        g_to[p][m][a][f][t] = at == KING ? 0 : g_thrL[(ac * 5 + at)][fo][T(t) ^ o];
                }
            }
        }
}

// -DCN_ROWSTATS: how many times each threat row is read (incremental updates and refreshes), written to
// cn_rowstats.bin (ROWS_THR little-endian uint64) at exit. Input of b6_consilium/make_thrperm.py.
#ifdef CN_ROWSTATS
struct RowStats {
    std::uint64_t n[ROWS_THR] = {};
    ~RowStats() {
        if (std::FILE* f = std::fopen("cn_rowstats.bin", "wb")) {
            std::fwrite(n, sizeof(n), 1, f);
            std::fclose(f);
        }
    }
};
RowStats g_rowStats;
    #define CN_ROWHIT(r, valid) (g_rowStats.n[r] += std::uint64_t(valid))
#else
    #define CN_ROWHIT(r, valid) ((void)0)
#endif

// -DCN_THRPERM: threat rows stored by frequency of use (table from make_thrperm.py): the rows a search reads most
// sit together at the start of the block, 90% of the reads in 12 MB instead of 65 MB spread over the whole table.
// Only the layout changes: g_thrSlot[spec row] = stored row. OFF by default: on the Xeon (18/10/2026, PGO, cycles per
// node) it cost 0.8 points with large pages and 0.85 without them in the middlegame (the table lookup per tuple
// outweighs the locality). Kept to be measured on other CPUs.
#ifdef CN_THRPERM
    #include "cn_thrperm.inc"   // THR_HOT[]: spec rows, most used first
std::uint16_t g_thrSlot[ROWS_THR];
void build_thr_slots() {
    static_assert(ROWS_THR <= 65536, "g_thrSlot holds 16-bit rows");
    std::vector<char> placed(ROWS_THR, 0);
    int               n = 0;
    for (int r : THR_HOT)
        if (r >= 0 && r < ROWS_THR && !placed[r]) {
            placed[r]     = 1;
            g_thrSlot[r] = std::uint16_t(n++);
        }
    for (int r = 0; r < ROWS_THR; r++)
        if (!placed[r]) g_thrSlot[r] = std::uint16_t(n++);
}
inline int thr_slot(int r) { return g_thrSlot[r]; }
#else
void build_thr_slots() {}
inline int thr_slot(int r) { return r; }
#endif

inline int threat_index_fast(int p, int m, int apc, int tpc, int f, int t) {
    const int base = g_tb[p][m][apc][tpc][f];
    if (base < 0 || (g_same[apc][tpc] & g_lt[p][m][f][t])) return -1;
    return base + g_to[p][m][apc][f][t];
}

// The same without a branch, for the update loops: the row (0 when the tuple has no feature) and valid = 0/1. Whether
// a tuple has a feature depends on the pieces and squares of each move, so the branch of threat_index_fast was
// mispredicted often.
inline int threat_row(int p, int m, int apc, int tpc, int f, int t, int& valid) {
    const int base = g_tb[p][m][apc][tpc][f];
    valid          = int(base >= 0) & (1 ^ int(g_same[apc][tpc] & g_lt[p][m][f][t]));
    return (base + g_to[p][m][apc][f][t]) & -valid;
}

bool check_fast_threats() {
    for (int p = 0; p < 2; p++)
        for (int m = 0; m < 2; m++)
            for (int a = 0; a < 12; a++)
                for (int d = 0; d < 12; d++)
                    for (int f = 0; f < 64; f++)
                        for (int t = 0; t < 64; t++)
                            if (threat_index_fast(p, m, a, d, f, t) != threat_index(p, orient(p, m), a, d, f, t))
                                return false;
    return true;
}

inline int pair_index(int a, int b) {
    const int hi = a > b ? a : b, lo = a > b ? b : a;
    return hi * (hi - 1) / 2 + lo;
}

u64 passers(int c, u64 own, u64 opp) {
    u64 out = 0;
    for (u64 b = own; b; b &= b - 1) {
        const int s = CN_LSB(b);
        if (!(opp & g_frontSpan[c][s]) && !(own & g_frontFile[c][s])) out |= 1ULL << s;
    }
    return out;
}

// ================================================================ accumulation
//
// Rows to add and to subtract, collected for a whole chain of moves and applied in one pass over the accumulator,
// one tile of registers at a time: every accumulator value is read and written once whatever the number of rows.
// Integer sums modulo 2^16 do not depend on the order (spec 4), so incremental and refresh give the same result.

struct RowList {
    static constexpr int CAP16 = 512, CAP8 = 1024;
    const Net* net;
    int n16a = 0, n16s = 0, n8a = 0, n8s = 0;
    const std::int16_t* a16[CAP16];
    const std::int16_t* s16[CAP16];
    const std::int8_t*  a8[CAP8];
    const std::int8_t*  s8[CAP8];
    explicit RowList(const Net* n) : net(n) {}
    // A row added (add = 1) or removed (add = 0) without a branch on the sign: written in both lists, only the right
    // counter moves. The PSQT values are read from the row tails (ROW16, ROW8).
    // The first line of the row and its PSQT line are requested here, as soon as the row is known: the rest of the row
    // is sequential and the hardware streamer brings it. (A separate pass over the lists afterwards cost eight loops
    // of variable length, 4.4% of the branch misses of the engine on 17/10; every line of every row saturated the
    // miss buffers.)
    void push8(const std::int8_t* w, int add) {
        CN_PREFETCH(w);
        CN_PREFETCH(w + L1);
        a8[n8a] = w;
        s8[n8s] = w;
        n8a += add;
        n8s += 1 - add;
    }
    // A threat row only if valid (threat_row), still without a branch: no counter moves when valid = 0.
    void pushT(int r, int valid, int add) {
        CN_ROWHIT(r, valid);
        const std::int8_t* w = net->wThr[thr_slot(r)];
        CN_PREFETCH(w);
        CN_PREFETCH(w + L1);
        a8[n8a] = w;
        s8[n8s] = w;
        n8a += add & valid;
        n8s += (1 - add) & valid;
    }
    void push16(const std::int16_t* w, int add) {
        CN_PREFETCH(w);
        CN_PREFETCH(w + L1);
        a16[n16a] = w;
        s16[n16s] = w;
        n16a += add;
        n16s += 1 - add;
    }
#ifdef TRIUMV_C2
    void pushH(int r, int sign) { push8(net->wHka[r], sign > 0); }
#else
    void pushH(int r, int sign) { push16(net->wHka[r], sign > 0); }
#endif
    void pushP(int r, int sign) { push8(net->wPP[r], sign > 0); }
    void pushS(int r, int sign) { push8(net->wPas[r], sign > 0); }
};

#ifdef TRIUMV_C2
// Consilium-2: the expert of perspective p, from the routing table over the non-pawn material signature of the side
// and of its opponent (the trainer's router::cell). Pawns never enter the key.
inline int side_sig(const Board& b, int c) {
    return router_sig(CN_POPCNT(b.bb[KNIGHT + 6 * c]), CN_POPCNT(b.bb[BISHOP + 6 * c]), CN_POPCNT(b.bb[ROOK + 6 * c]),
                      CN_POPCNT(b.bb[QUEEN + 6 * c]));
}
#ifdef CN_C2_ONE_EXPERT
// Cost diagnosis only (21/10/2026): every perspective on expert 0, so no expert change ever forces a refresh.
inline int expert_of(const Board&, int) { return 0; }
#else
inline int expert_of(const Board& b, int p) { return g_router[side_sig(b, p) * ROUTER_SIG + side_sig(b, p ^ 1)]; }
#endif
#endif

// Context of one perspective: everything an index needs besides the piece and square.
struct Persp {
    int p, ksq, phase, mh, mo, oo;
    int                 hbase;   // phase * rows of a phase + king bucket * 704
    const std::int16_t (*hk)[64];
    const std::int8_t  (*pd)[64];
    Persp(int p_, const Board& b) : p(p_) {
        ksq   = CN_LSB(b.bb[KING + 6 * p]);
#ifdef TRIUMV_C2
        phase = expert_of(b, p);
#else
        phase = g_phase_map[CN_POPCNT(b.occ[2])];
#endif
        mh    = mirror_hka(ksq);
        mo    = mirror_oth(ksq);
        oo    = orient(p, mo);
        hbase = phase * HKA_PHASE_ROWS + g_kb[p][ksq];
        hk    = g_hk[p][mh];
        pd    = g_pid[p][mo];
    }
    int hka(int pc, int s) const { return hbase + hk[pc][s]; }
    int pid(int c, int s) const { return pd[c][s]; }
};

void apply_psqt(const RowList& L, const std::int32_t* src, std::int32_t* dst) {
#if defined(USE_AVX2) || defined(USE_AVX512)
    __m256i t = _mm256_loadu_si256((const __m256i*)src);
    for (int i = 0; i < L.n16a; i++) t = _mm256_add_epi32(t, _mm256_load_si256((const __m256i*)(L.a16[i] + L1)));
    for (int i = 0; i < L.n16s; i++) t = _mm256_sub_epi32(t, _mm256_load_si256((const __m256i*)(L.s16[i] + L1)));
    for (int i = 0; i < L.n8a; i++) t = _mm256_add_epi32(t, _mm256_load_si256((const __m256i*)(L.a8[i] + L1)));
    for (int i = 0; i < L.n8s; i++) t = _mm256_sub_epi32(t, _mm256_load_si256((const __m256i*)(L.s8[i] + L1)));
    _mm256_storeu_si256((__m256i*)dst, t);
#else
    std::int32_t t[NPSQT];
    for (int k = 0; k < NPSQT; k++) t[k] = src[k];
    auto tail = [](const void* row, std::size_t bytes) {
        return reinterpret_cast<const std::int32_t*>(static_cast<const char*>(row) + bytes);
    };
    for (int i = 0; i < L.n16a; i++) for (int k = 0; k < NPSQT; k++) t[k] += tail(L.a16[i], 2 * L1)[k];
    for (int i = 0; i < L.n16s; i++) for (int k = 0; k < NPSQT; k++) t[k] -= tail(L.s16[i], 2 * L1)[k];
    for (int i = 0; i < L.n8a; i++) for (int k = 0; k < NPSQT; k++) t[k] += tail(L.a8[i], L1)[k];
    for (int i = 0; i < L.n8s; i++) for (int k = 0; k < NPSQT; k++) t[k] -= tail(L.s8[i], L1)[k];
    for (int k = 0; k < NPSQT; k++) dst[k] = t[k];
#endif
}

#if defined(USE_AVX512)
// The int8 rows of a list (added and removed) on R registers of 32 int16 values from column t, with their PSQT tails
// (WithPsqt). CN_W1: one 64-byte load per pair of registers (R even, t a multiple of 64).
template <int R, bool WithPsqt>
inline void rows8_512(const RowList& L, __m512i* r, __m256i& ps, int t) {
    constexpr int W = 32;
#ifdef CN_W1
    const __m512i even = _mm512_set1_epi16(1);   // u8 pairs (1, 0): vpmaddubsw keeps the signed even byte
    for (int i = 0; i < L.n8a; i++) {
        for (int k = 0; k < R; k += 2) {
            const __m512i w = _mm512_load_si512(L.a8[i] + t + k * W);
            r[k]     = _mm512_add_epi16(r[k], _mm512_maddubs_epi16(even, w));
            r[k + 1] = _mm512_add_epi16(r[k + 1], _mm512_srai_epi16(w, 8));
        }
        if (WithPsqt) ps = _mm256_add_epi32(ps, _mm256_load_si256((const __m256i*)(L.a8[i] + L1)));
    }
    for (int i = 0; i < L.n8s; i++) {
        for (int k = 0; k < R; k += 2) {
            const __m512i w = _mm512_load_si512(L.s8[i] + t + k * W);
            r[k]     = _mm512_sub_epi16(r[k], _mm512_maddubs_epi16(even, w));
            r[k + 1] = _mm512_sub_epi16(r[k + 1], _mm512_srai_epi16(w, 8));
        }
        if (WithPsqt) ps = _mm256_sub_epi32(ps, _mm256_load_si256((const __m256i*)(L.s8[i] + L1)));
    }
#else
    for (int i = 0; i < L.n8a; i++) {
        for (int k = 0; k < R; k++)
            r[k] = _mm512_add_epi16(r[k], _mm512_cvtepi8_epi16(_mm256_load_si256((const __m256i*)(L.a8[i] + t + k * W))));
        if (WithPsqt) ps = _mm256_add_epi32(ps, _mm256_load_si256((const __m256i*)(L.a8[i] + L1)));
    }
    for (int i = 0; i < L.n8s; i++) {
        for (int k = 0; k < R; k++)
            r[k] = _mm512_sub_epi16(r[k], _mm512_cvtepi8_epi16(_mm256_load_si256((const __m256i*)(L.s8[i] + t + k * W))));
        if (WithPsqt) ps = _mm256_sub_epi32(ps, _mm256_load_si256((const __m256i*)(L.s8[i] + L1)));
    }
#endif
}

// AVX-512: 1024 values = 32 registers; two tiles of 16 registers (512 values). The first tile also sums the PSQT
// values at the row tails (WithPsqt): four loops over the rows instead of eight, and each loop exit is a branch that
// depends on the list lengths.
template <bool WithPsqt>
inline void apply_tile(const RowList& L, const Acc& src, Acc& dst, int t) {
    constexpr int R = 16, W = 32;
    __m512i       r[R];
    __m256i       ps = WithPsqt ? _mm256_loadu_si256((const __m256i*)src.psqt) : _mm256_setzero_si256();
    for (int k = 0; k < R; k++) r[k] = _mm512_loadu_si512(src.v + t + k * W);
    for (int i = 0; i < L.n16a; i++) {
        for (int k = 0; k < R; k++) r[k] = _mm512_add_epi16(r[k], _mm512_load_si512(L.a16[i] + t + k * W));
        if (WithPsqt) ps = _mm256_add_epi32(ps, _mm256_load_si256((const __m256i*)(L.a16[i] + L1)));
    }
    for (int i = 0; i < L.n16s; i++) {
        for (int k = 0; k < R; k++) r[k] = _mm512_sub_epi16(r[k], _mm512_load_si512(L.s16[i] + t + k * W));
        if (WithPsqt) ps = _mm256_sub_epi32(ps, _mm256_load_si256((const __m256i*)(L.s16[i] + L1)));
    }
    rows8_512<R, WithPsqt>(L, r, ps, t);
    for (int k = 0; k < R; k++) _mm512_storeu_si512(dst.v + t + k * W, r[k]);
    if (WithPsqt) _mm256_storeu_si256((__m256i*)dst.psqt, ps);
}

void apply(const RowList& L, const Acc& src, Acc& dst) {
    CN_TIME(3);
    static_assert(L1 == 2 * 16 * 32, "apply: two tiles of 16 registers");
    apply_tile<true>(L, src, dst, 0);
    apply_tile<false>(L, src, dst, L1 / 2);
}
#elif defined(USE_AVX2)
// The int16 and the int8 rows of a list (added and removed) on R registers of 16 values from column t, with their PSQT
// tails (WithPsqt). Shared by apply and, since 21/10/2026, by the single-pass refresh and king step.
template <int R, bool WithPsqt>
inline void rows16_256(const RowList& L, __m256i* r, __m256i& ps, int t) {
    constexpr int W = 16;
    for (int i = 0; i < L.n16a; i++) {
        for (int k = 0; k < R; k++)
            r[k] = _mm256_add_epi16(r[k], _mm256_load_si256((const __m256i*)(L.a16[i] + t + k * W)));
        if (WithPsqt) ps = _mm256_add_epi32(ps, _mm256_load_si256((const __m256i*)(L.a16[i] + L1)));
    }
    for (int i = 0; i < L.n16s; i++) {
        for (int k = 0; k < R; k++)
            r[k] = _mm256_sub_epi16(r[k], _mm256_load_si256((const __m256i*)(L.s16[i] + t + k * W)));
        if (WithPsqt) ps = _mm256_sub_epi32(ps, _mm256_load_si256((const __m256i*)(L.s16[i] + L1)));
    }
}

template <int R, bool WithPsqt>
inline void rows8_256(const RowList& L, __m256i* r, __m256i& ps, int t) {
    constexpr int W = 16;
#ifdef CN_W1
    // Interleaved int8 rows (CN_W1): one 32-byte load per pair of registers, widened on ports 0/1.
    const __m256i even = _mm256_set1_epi16(1);   // u8 pairs (1, 0): vpmaddubsw keeps the signed even byte
    for (int i = 0; i < L.n8a; i++) {
        for (int k = 0; k < R; k += 2) {
            const __m256i w = _mm256_load_si256((const __m256i*)(L.a8[i] + t + k * W));
            r[k]     = _mm256_add_epi16(r[k], _mm256_maddubs_epi16(even, w));
            r[k + 1] = _mm256_add_epi16(r[k + 1], _mm256_srai_epi16(w, 8));
        }
        if (WithPsqt) ps = _mm256_add_epi32(ps, _mm256_load_si256((const __m256i*)(L.a8[i] + L1)));
    }
    for (int i = 0; i < L.n8s; i++) {
        for (int k = 0; k < R; k += 2) {
            const __m256i w = _mm256_load_si256((const __m256i*)(L.s8[i] + t + k * W));
            r[k]     = _mm256_sub_epi16(r[k], _mm256_maddubs_epi16(even, w));
            r[k + 1] = _mm256_sub_epi16(r[k + 1], _mm256_srai_epi16(w, 8));
        }
        if (WithPsqt) ps = _mm256_sub_epi32(ps, _mm256_load_si256((const __m256i*)(L.s8[i] + L1)));
    }
#else
    for (int i = 0; i < L.n8a; i++) {
        for (int k = 0; k < R; k++)
            r[k] = _mm256_add_epi16(r[k], _mm256_cvtepi8_epi16(_mm_load_si128((const __m128i*)(L.a8[i] + t + k * W))));
        if (WithPsqt) ps = _mm256_add_epi32(ps, _mm256_load_si256((const __m256i*)(L.a8[i] + L1)));
    }
    for (int i = 0; i < L.n8s; i++) {
        for (int k = 0; k < R; k++)
            r[k] = _mm256_sub_epi16(r[k], _mm256_cvtepi8_epi16(_mm_load_si128((const __m128i*)(L.s8[i] + t + k * W))));
        if (WithPsqt) ps = _mm256_sub_epi32(ps, _mm256_load_si256((const __m256i*)(L.s8[i] + L1)));
    }
#endif
}

// AVX2: 16 registers; tiles of 8 registers (128 values), eight tiles. As on AVX-512, the first tile also sums the PSQT
// values at the row tails (WithPsqt, a ninth register): apply_psqt afterwards ran four more loops over the rows, each
// with an exit branch on a list length that changes from move to move (19/10/2026). -DCN_PSQT_SEPARATE goes back.
template <bool WithPsqt>
inline void apply_tile(const RowList& L, const Acc& src, Acc& dst, int t) {
    constexpr int R = 8, W = 16;
    __m256i       r[R];
    __m256i       ps = WithPsqt ? _mm256_loadu_si256((const __m256i*)src.psqt) : _mm256_setzero_si256();
    for (int k = 0; k < R; k++) r[k] = _mm256_loadu_si256((const __m256i*)(src.v + t + k * W));
    rows16_256<R, WithPsqt>(L, r, ps, t);
    rows8_256<R, WithPsqt>(L, r, ps, t);
    for (int k = 0; k < R; k++) _mm256_storeu_si256((__m256i*)(dst.v + t + k * W), r[k]);
    if (WithPsqt) _mm256_storeu_si256((__m256i*)dst.psqt, ps);
}

void apply(const RowList& L, const Acc& src, Acc& dst) {
    CN_TIME(3);
    constexpr int TILE = 8 * 16;
#ifdef CN_PSQT_SEPARATE
    for (int t = 0; t < L1; t += TILE) apply_tile<false>(L, src, dst, t);
    apply_psqt(L, src.psqt, dst.psqt);
#else
    apply_tile<true>(L, src, dst, 0);
    for (int t = TILE; t < L1; t += TILE) apply_tile<false>(L, src, dst, t);
#endif
}
#else
void apply(const RowList& L, const Acc& src, Acc& dst) {
    CN_TIME(3);
    alignas(64) std::uint16_t t[L1];
    for (int j = 0; j < L1; j++) t[j] = std::uint16_t(src.v[j]);
    for (int i = 0; i < L.n16a; i++) for (int j = 0; j < L1; j++) t[j] += std::uint16_t(L.a16[i][j]);
    for (int i = 0; i < L.n16s; i++) for (int j = 0; j < L1; j++) t[j] -= std::uint16_t(L.s16[i][j]);
    for (int i = 0; i < L.n8a; i++) for (int j = 0; j < L1; j++) t[j] += std::uint16_t(std::int16_t(L.a8[i][j]));
    for (int i = 0; i < L.n8s; i++) for (int j = 0; j < L1; j++) t[j] -= std::uint16_t(std::int16_t(L.s8[i][j]));
    for (int j = 0; j < L1; j++) dst.v[j] = std::int16_t(t[j]);
    apply_psqt(L, src.psqt, dst.psqt);
}
#endif

// Active features of one perspective (spec 3): the threat, pawn-pair and passed-pawn blocks (part O).
void active_other(const Board& b, const Persp& P, RowList& L) {
    const u64 occ = b.occ[2];
    const u64 kings = b.bb[KING] | b.bb[KING + 6];
    const u64 pawns = b.bb[PAWN] | b.bb[PAWN + 6];
    const u64 noK = occ & ~kings;
    for (int c = 0; c < 2; c++) {
        for (u64 x = b.bb[PAWN + 6 * c]; x; x &= x - 1) {
            const int f = CN_LSB(x);
            for (u64 t = pawn_attacks[c][f] & noK & ~pawns; t; t &= t - 1) {
                const int to = CN_LSB(t);
                int       valid;
                const int i = threat_row(P.p, P.mo, PAWN + 6 * c, b.mb[to], f, to, valid);
                L.pushT(i, valid, 1);
            }
        }
        for (int pt = KNIGHT; pt <= QUEEN; pt++)
            for (u64 x = b.bb[pt + 6 * c]; x; x &= x - 1) {
                const int f = CN_LSB(x);
                u64 att = pt == KNIGHT ? knight_attacks[f]
                        : pt == BISHOP ? get_bishop_attacks(f, occ)
                        : pt == ROOK   ? get_rook_attacks(f, occ)
                                       : get_bishop_attacks(f, occ) | get_rook_attacks(f, occ);
                for (u64 t = att & noK; t; t &= t - 1) {
                    const int to = CN_LSB(t);
                    int       valid;
                    const int i = threat_row(P.p, P.mo, pt + 6 * c, b.mb[to], f, to, valid);
                    L.pushT(i, valid, 1);
                }
            }
    }

    const u64 wp = b.bb[PAWN], bp = b.bb[PAWN + 6];
    for (u64 x = pawns; x; x &= x - 1) {
        const int s = CN_LSB(x);
        const int cs = (bp >> s) & 1;
        for (u64 t = g_ppMask[s] & pawns & ((1ULL << s) - 1); t; t &= t - 1) {   // each pair once
            const int to = CN_LSB(t);
            L.pushP(pair_index(P.pid(cs, s), P.pid((bp >> to) & 1, to)), 1);
        }
    }
    const u64 pw = passers(0, wp, bp), pb = passers(1, bp, wp);
    for (u64 x = pw; x; x &= x - 1) L.pushS(P.pid(0, CN_LSB(x)), 1);
    for (u64 x = pb; x; x &= x - 1) L.pushS(P.pid(1, CN_LSB(x)), 1);
}


// All active features (both parts), for the evaluation from scratch.
void active_rows(const Board& b, const Persp& P, RowList& L) {
    for (int pc = 0; pc < 12; pc++)
        for (u64 x = b.bb[pc]; x; x &= x - 1)
            L.pushH(P.hka(pc, CN_LSB(x)), 1);
    active_other(b, P, L);
}

void refresh_one(const Net& net, const Board& b, int p, Acc& a) {
    std::memcpy(a.v, net.ftBias, sizeof(a.v));
    std::memset(a.psqt, 0, sizeof(a.psqt));
    RowList L(&net);
    active_rows(b, Persp(p, b), L);
    apply(L, a, a);
}

void kcache_init(const Net& net, Handle& h) {
    for (auto& a : h.kcache)
        for (auto& b : a)
            for (KingCacheEntry& e : b) {
                std::memcpy(e.acc.v, net.ftBias, sizeof(e.acc.v));
                std::memset(e.acc.psqt, 0, sizeof(e.acc.psqt));
                std::memset(e.bb, 0, sizeof(e.bb));
            }
    h.cacheNet = net.contentHash;
}

// The rows that bring the cache entry of this king square and phase to the board: only the pieces that differ from
// last time (the entry's bitboards are updated here; its accumulator by whoever applies the rows).
KingCacheEntry& kcache_rows(const Net& net, Handle& h, const Board& b, const Persp& P, RowList& L) {
    if (h.cacheNet != net.contentHash) kcache_init(net, h);
    KingCacheEntry& e = h.kcache[P.p][P.ksq][P.phase];
    for (int pc = 0; pc < 12; pc++) {
        for (u64 x = e.bb[pc] & ~b.bb[pc]; x; x &= x - 1) L.pushH(P.hka(pc, CN_LSB(x)), -1);
        for (u64 x = b.bb[pc] & ~e.bb[pc]; x; x &= x - 1) L.pushH(P.hka(pc, CN_LSB(x)), 1);
        e.bb[pc] = b.bb[pc];
    }
    return e;
}

// The rows one move changes for perspective P (spec 6), from the dirty state the engine wrote.
// Part H: the pieces of the move.
void move_rows_h(const NnStack& S, int i, const Persp& P, RowList& L, int dir) {
    const NnDirtyPiece& dp = S.st[i].dp;
    L.pushH(P.hka(dp.pc, dp.from), -1 * dir);
    if (dp.to != NN_SQ_NONE) L.pushH(P.hka(dp.pc, dp.to), 1 * dir);
    if (dp.remove_sq != NN_SQ_NONE) L.pushH(P.hka(dp.remove_pc, dp.remove_sq), -1 * dir);
    if (dp.add_sq != NN_SQ_NONE) L.pushH(P.hka(dp.add_pc, dp.add_sq), 1 * dir);
}

// Part O: threat tuples, pawn pairs, passed pawns of the move.
// Threat tuples of move i for one or both perspectives (PP[p] == nullptr: skipped), each tuple decoded once.
void move_threats(const NnStack& S, int i, const Persp* const PP[2], RowList* const LL[2], int dir) {
    CN_TIME(2);
    const NnDirtyThreats& th = S.st[i].threats;
    const int m0 = PP[0] ? PP[0]->mo : 0, m1 = PP[1] ? PP[1]->mo : 0;
    for (std::uint32_t k = 0; k < th.n; k++) {
        const std::uint32_t t   = th.list[k];
        const int           apc = (t >> NN_THR_PC) & 15, tpc = (t >> NN_THR_TPC) & 15;
        const int           f = (t >> NN_THR_PCSQ) & 255, to = (t >> NN_THR_TSQ) & 255;
        const int           add = int((t >> 31) ^ (dir < 0));
        if (PP[0]) {
            int       valid;
            const int r = threat_row(0, m0, apc, tpc, f, to, valid);
            LL[0]->pushT(r, valid, add);
        }
        if (PP[1]) {
            int       valid;
            const int r = threat_row(1, m1, apc, tpc, f, to, valid);
            LL[1]->pushT(r, valid, add);
        }
    }
}

// Pawn pairs and passed pawns of move i (only on moves that touch pawns).
void move_pawns(const NnStack& S, int i, const Persp& P, RowList& L, int dir);

void move_rows_o(const NnStack& S, int i, const Persp& P, RowList& L, int dir) {
    const Persp*   PP[2] = {P.p == 0 ? &P : nullptr, P.p == 1 ? &P : nullptr};
    RowList* const LL[2] = {P.p == 0 ? &L : nullptr, P.p == 1 ? &L : nullptr};
    move_threats(S, i, PP, LL, dir);
    move_pawns(S, i, P, L, dir);
}

void move_pawns(const NnStack& S, int i, const Persp& P, RowList& L, int dir) {
    const NnState&      st = S.st[i];
    const NnDirtyPawns& pw = st.pawns;
    if (pw.any) {
        u64 cur[2] = {pw.before[0], pw.before[1]};
        for (int k = 0; k < pw.nRemoved; k++) {
            const int s = pw.removedSq[k], c = pw.removedC[k];
            cur[c] &= ~(1ULL << s);
            for (u64 t = g_ppMask[s] & (cur[0] | cur[1]); t; t &= t - 1) {
                const int to = CN_LSB(t);
                L.pushP(pair_index(P.pid(c, s), P.pid((cur[1] >> to) & 1, to)), -1 * dir);
            }
        }
        if (pw.addedSq != NN_SQ_NONE) {
            const int s = pw.addedSq, c = pw.addedC;
            for (u64 t = g_ppMask[s] & (cur[0] | cur[1]); t; t &= t - 1) {
                const int to = CN_LSB(t);
                L.pushP(pair_index(P.pid(c, s), P.pid((cur[1] >> to) & 1, to)), 1 * dir);
            }
        }
        const u64 bw = S.v1PassW[i - 1], aw = S.v1PassW[i], bb = S.v1PassB[i - 1], ab = S.v1PassB[i];
        for (u64 x = bw & ~aw; x; x &= x - 1) L.pushS(P.pid(0, CN_LSB(x)), -1 * dir);
        for (u64 x = aw & ~bw; x; x &= x - 1) L.pushS(P.pid(0, CN_LSB(x)), 1 * dir);
        for (u64 x = bb & ~ab; x; x &= x - 1) L.pushS(P.pid(1, CN_LSB(x)), -1 * dir);
        for (u64 x = ab & ~bb; x; x &= x - 1) L.pushS(P.pid(1, CN_LSB(x)), 1 * dir);
    }
}

// ================================================================ forward (spec 5)

// Layers after l1 (spec 5.3-5.8): small, the same code for every instruction set.
Out forward_tail(const Stack& st, const std::int32_t* x1, const Acc& us, const Acc& them, int bucket) {
    const std::int32_t skip = x1[L2 - 2] - x1[L2 - 1];
    std::uint8_t a1[2 * L2];
    for (int o = 0; o < L2; o++) {
        const std::int64_t q = std::int64_t(x1[o]) * x1[o];
        a1[o]      = std::uint8_t(std::min<std::int64_t>(q >> 21, 127));
        a1[L2 + o] = std::uint8_t(std::clamp(x1[o] >> 7, 0, 127));
    }
    std::int32_t x2[L3];
    for (int o = 0; o < L3; o++) {
        std::int32_t sum = st.b2[o];
        for (int i = 0; i < 2 * L2; i++) sum += st.w2[o][i] * a1[i];
        x2[o] = sum;
    }
    std::uint8_t a2[2 * L3];
    for (int o = 0; o < L3; o++) {
        const std::int64_t q = std::int64_t(x2[o]) * x2[o];
        a2[o]      = std::uint8_t(std::min<std::int64_t>(q >> 19, 127));
        a2[L3 + o] = std::uint8_t(std::clamp(x2[o] >> 6, 0, 127));
    }
    std::int32_t out = st.b3;
    for (int i = 0; i < 2 * L2; i++) out += st.w3[i] * a1[i];
    for (int i = 0; i < 2 * L3; i++) out += st.w3[2 * L2 + i] * a2[i];
    Out r;
    r.positional = int((std::int64_t(out) + skip) * 600 / 16384);   // C++ division truncates toward zero
    r.psqt       = (us.psqt[bucket] - them.psqt[bucket]) / 32;
    return r;
}

// Consilium-2: the forward also takes the 32 outputs of the pawn-king subnetwork (units of the l1 sums), added to the
// l1 sums before the activations and the skip.
#ifdef TRIUMV_C2
// x1out (C2 signals of agent B, 21/10/2026): when given, the l1 sums are left there (the evaluation keeps them in the
// thread's handle, Handle::x1, where the AVX-512 path stores them instead of on the stack); read by pk_effect.
    #define CN_PK_PARAM , const std::int32_t* pk, std::int32_t* x1out = nullptr
    #define CN_PK_ARG(x) , x
#else
    #define CN_PK_PARAM
    #define CN_PK_ARG(x)
#endif

// Reference forward, plain C++ (spec 5 as written).
Out forward_scalar(const Net& net, const Acc& us, const Acc& them, int bucket CN_PK_PARAM) {
    alignas(64) std::uint8_t l0[L1];
    const Acc* side[2] = {&us, &them};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < L1H; j++) {
            const int a = std::clamp<int>(side[i]->v[j], 0, 255);
            const int b = std::clamp<int>(side[i]->v[j + L1H], 0, 255);
            l0[i * L1H + j] = std::uint8_t((a * b) >> 9);
        }
    const Stack& st = net.stack[bucket];
    std::int32_t x1[L2];
    for (int o = 0; o < L2; o++) {
        std::int32_t sum = st.b1[o];
#ifdef TRIUMV_C2
        sum += pk[o];
#endif
        for (int i = 0; i < L1; i++) sum += st.w1[o][i] * l0[i];
        x1[o] = sum;
    }
#ifdef TRIUMV_C2
    if (x1out) std::memcpy(x1out, x1, sizeof(x1));
#endif
    return forward_tail(st, x1, us, them, bucket);
}

#if defined(USE_AVX512)
// l0 of one side (spec 5.1) and its nonzero 4-byte groups, appended to nz (returns the new count).
// (a*b) >> 9 = mulhi(a << 7, b) for 0 <= a, b <= 255. b is cut from above only: a negative b gives a product <= 0,
// which packus turns into 0, the same as clamping b to 0. packus works per 128-bit lane, so l0 stays in its order
// (8-byte chunks of the two sources interleaved) and the l1 weights were stored in that order at load (l0_group).
inline int l0_side(const Acc& a, std::uint8_t* out, std::int32_t* nz, int n, int firstGroup) {
    const __m512i zero = _mm512_setzero_si512(), top = _mm512_set1_epi16(255);
    __m512i       idx  = _mm512_add_epi32(_mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
                                          _mm512_set1_epi32(firstGroup));
    for (int j = 0; j < L1H; j += 64) {
        __m512i x0 = _mm512_loadu_si512(a.v + j), x1 = _mm512_loadu_si512(a.v + j + 32);
        __m512i y0 = _mm512_loadu_si512(a.v + L1H + j), y1 = _mm512_loadu_si512(a.v + L1H + j + 32);
        x0 = _mm512_slli_epi16(_mm512_min_epi16(_mm512_max_epi16(x0, zero), top), 7);
        x1 = _mm512_slli_epi16(_mm512_min_epi16(_mm512_max_epi16(x1, zero), top), 7);
        y0 = _mm512_min_epi16(y0, top);
        y1 = _mm512_min_epi16(y1, top);
        const __m512i   p = _mm512_packus_epi16(_mm512_mulhi_epi16(x0, y0), _mm512_mulhi_epi16(x1, y1));
        const __mmask16 m = _mm512_test_epi32_mask(p, p);
        _mm512_store_si512(out + j, p);
        // Compacted in registers: the memory form of compress is slow on Zen 4.
        _mm512_storeu_si512(nz + n, _mm512_maskz_compress_epi32(m, idx));
        n += CN_POPCNT(unsigned(m));
        idx = _mm512_add_epi32(idx, _mm512_set1_epi32(16));
    }
    return n;
}

inline __m512i dot4(__m512i acc, __m512i x, __m512i w) {
    #if defined(USE_VNNI)
    return _mm512_dpbusd_epi32(acc, x, w);
    #else
    // u8 x s8 pairs in int16 cannot saturate: l0 <= 127, 2 * 127 * 127 < 32767.
    return _mm512_add_epi32(acc, _mm512_madd_epi16(_mm512_maddubs_epi16(x, w), _mm512_set1_epi16(1)));
    #endif
}


// Layers after l1 in registers (spec 5.3-5.8). Squares of |x| cut at 16383 (l1) and 8191 (l2), which is exact
// (spec 5.4), so 32-bit products cannot overflow.
inline void act16(__m512i x, int cut, int sqShift, int vShift, __m128i& sq, __m128i& val) {
    const __m512i zero = _mm512_setzero_si512(), top = _mm512_set1_epi32(127);
    const __m512i ax = _mm512_min_epi32(_mm512_abs_epi32(x), _mm512_set1_epi32(cut));
    const __m512i q  = _mm512_min_epi32(_mm512_srli_epi32(_mm512_mullo_epi32(ax, ax), sqShift), top);
    const __m512i v  = _mm512_min_epi32(_mm512_max_epi32(_mm512_srai_epi32(x, vShift), zero), top);
    sq  = _mm512_cvtepi32_epi8(q);
    val = _mm512_cvtepi32_epi8(v);
}

Out forward_tail_simd(const Stack& st, const std::int32_t* x1, const Acc& us, const Acc& them, int bucket) {
    alignas(64) std::uint8_t a1[2 * L2], a2[2 * L3];
    const __m512i x1lo = _mm512_load_si512(x1), x1hi = _mm512_load_si512(x1 + 16);
    __m128i q0, v0, q1, v1;
    act16(x1lo, 16383, 21, 7, q0, v0);
    act16(x1hi, 16383, 21, 7, q1, v1);
    _mm_store_si128((__m128i*)(a1), q0);
    _mm_store_si128((__m128i*)(a1 + 16), q1);
    _mm_store_si128((__m128i*)(a1 + 32), v0);
    _mm_store_si128((__m128i*)(a1 + 48), v1);

    const std::int32_t* in = reinterpret_cast<const std::int32_t*>(a1);
    __m512i c0 = _mm512_loadu_si512(st.b2), c1 = _mm512_loadu_si512(st.b2 + 16);
    for (int g = 0; g < 2 * L2 / 4; g++) {
        const __m512i  x = _mm512_set1_epi32(in[g]);
        const __m512i* w = reinterpret_cast<const __m512i*>(st.w2g[g]);
        c0 = dot4(c0, x, _mm512_load_si512(w));
        c1 = dot4(c1, x, _mm512_load_si512(w + 1));
    }
    act16(c0, 8191, 19, 6, q0, v0);
    act16(c1, 8191, 19, 6, q1, v1);
    _mm_store_si128((__m128i*)(a2), q0);
    _mm_store_si128((__m128i*)(a2 + 16), q1);
    _mm_store_si128((__m128i*)(a2 + 32), v0);
    _mm_store_si128((__m128i*)(a2 + 48), v1);

    __m512i o = dot4(_mm512_setzero_si512(), _mm512_load_si512(a1), _mm512_loadu_si512(st.w3));
    o = dot4(o, _mm512_load_si512(a2), _mm512_loadu_si512(st.w3 + 2 * L2));
    const std::int32_t out  = st.b3 + _mm512_reduce_add_epi32(o);
    const std::int32_t skip = x1[L2 - 2] - x1[L2 - 1];
    Out r;
    r.positional = int((std::int64_t(out) + skip) * 600 / 16384);
    r.psqt       = (us.psqt[bucket] - them.psqt[bucket]) / 32;
    return r;
}

Out forward_simd(const Net& net, const Acc& us, const Acc& them, int bucket CN_PK_PARAM) {
    CN_TIME(4);
    alignas(64) std::uint8_t l0[L1];
    alignas(64) std::int32_t nz[L1 / 4 + 16];
    int n = l0_side(us, l0, nz, 0, 0);
    n     = l0_side(them, l0 + L1H, nz, n, L1H / 4);
    CN_COUNT(6, n);   // -DCN_PROF: non-zero groups of 4 inputs per forward (the "cycles/call" column is the mean)
    const Stack&        st = net.stack[bucket];
    const std::int32_t* in = reinterpret_cast<const std::int32_t*>(l0);
    __m512i a0 = _mm512_loadu_si512(st.b1), a1 = _mm512_loadu_si512(st.b1 + 16);
#ifdef TRIUMV_C2
    a0 = _mm512_add_epi32(a0, _mm512_loadu_si512(pk));
    a1 = _mm512_add_epi32(a1, _mm512_loadu_si512(pk + 16));
#endif
    __m512i a2 = _mm512_setzero_si512(), a3 = a2;
    int i = 0;
#if CN_DOT_CHAINS >= 3 && defined(USE_VNNI)
    // Three independent chains (vpdpbusd has a latency of about five cycles); integer sums, same result.
    __m512i a4 = a2, a5 = a2;
    for (; i + 2 < n; i += 3) {
        const __m512i  x0 = _mm512_set1_epi32(in[nz[i]]), x1 = _mm512_set1_epi32(in[nz[i + 1]]);
        const __m512i  x2 = _mm512_set1_epi32(in[nz[i + 2]]);
        const __m512i* w0 = reinterpret_cast<const __m512i*>(st.w1g[nz[i]]);
        const __m512i* w1 = reinterpret_cast<const __m512i*>(st.w1g[nz[i + 1]]);
        const __m512i* w2 = reinterpret_cast<const __m512i*>(st.w1g[nz[i + 2]]);
        a0 = dot4(a0, x0, _mm512_load_si512(w0));
        a1 = dot4(a1, x0, _mm512_load_si512(w0 + 1));
        a2 = dot4(a2, x1, _mm512_load_si512(w1));
        a3 = dot4(a3, x1, _mm512_load_si512(w1 + 1));
        a4 = dot4(a4, x2, _mm512_load_si512(w2));
        a5 = dot4(a5, x2, _mm512_load_si512(w2 + 1));
    }
    a0 = _mm512_add_epi32(a0, a4);
    a1 = _mm512_add_epi32(a1, a5);
#endif
    for (; i + 1 < n; i += 2) {
        const int g0 = nz[i], g1 = nz[i + 1];
        const __m512i  x0 = _mm512_set1_epi32(in[g0]), x1 = _mm512_set1_epi32(in[g1]);
        const __m512i* w0 = reinterpret_cast<const __m512i*>(st.w1g[g0]);
        const __m512i* w1 = reinterpret_cast<const __m512i*>(st.w1g[g1]);
        a0 = dot4(a0, x0, _mm512_load_si512(w0));
        a1 = dot4(a1, x0, _mm512_load_si512(w0 + 1));
        a2 = dot4(a2, x1, _mm512_load_si512(w1));
        a3 = dot4(a3, x1, _mm512_load_si512(w1 + 1));
    }
    if (i < n) {
        const int      g  = nz[i];
        const __m512i  x  = _mm512_set1_epi32(in[g]);
        const __m512i* w  = reinterpret_cast<const __m512i*>(st.w1g[g]);
        a0 = dot4(a0, x, _mm512_load_si512(w));
        a1 = dot4(a1, x, _mm512_load_si512(w + 1));
    }
    alignas(64) std::int32_t x1loc[L2];
#ifdef TRIUMV_C2
    std::int32_t* x1 = x1out ? x1out : x1loc;   // x1out is 64-byte aligned (Handle::x1)
#else
    std::int32_t* x1 = x1loc;
#endif
    _mm512_store_si512(x1, _mm512_add_epi32(a0, a2));
    _mm512_store_si512(x1 + 16, _mm512_add_epi32(a1, a3));
    return forward_tail_simd(st, x1, us, them, bucket);
}
#elif defined(USE_AVX2)
// AVX2 (block 7 of 8.1, 18/10/2026: brought level with the AVX-512 path). Same steps: l0 kept in the order packus
// leaves it (l0_group maps the l1 weights to it), nonzero groups compacted without branches, l1 sparse, l2 and the
// output in registers. With AVX-VNNI (Intel Alder Lake and later without AVX-512) the u8 x s8 products use vpdpbusd
// in its VEX form, one instruction instead of three; the result is the same (inputs <= 127, no 16-bit saturation).
inline __m256i dot8(__m256i acc, __m256i x, __m256i w) {
    #if defined(USE_AVXVNNI)
    return _mm256_dpbusd_avx_epi32(acc, x, w);
    #else
    return _mm256_add_epi32(acc, _mm256_madd_epi16(_mm256_maddubs_epi16(x, w), _mm256_set1_epi16(1)));
    #endif
}

// g_nzIdx[m]: the positions of the set bits of the 8-bit mask m, as 8 bytes (unused bytes zero).
alignas(64) std::uint8_t g_nzIdx[256][8];
struct NzIdxInit {
    NzIdxInit() {
        for (int m = 0; m < 256; m++) {
            int k = 0;
            for (int b = 0; b < 8; b++)
                if (m & (1 << b)) g_nzIdx[m][k++] = std::uint8_t(b);
            while (k < 8) g_nzIdx[m][k++] = 0;
        }
    }
} g_nzIdxInit;

// l0 of one side (32 values per step) and its nonzero 4-byte groups, appended to nz; see the AVX-512 version.
inline int l0_side(const Acc& a, std::uint8_t* out, std::int32_t* nz, int n, int firstGroup) {
    const __m256i zero = _mm256_setzero_si256(), top = _mm256_set1_epi16(255);
    for (int j = 0; j < L1H; j += 32) {
        __m256i x0 = _mm256_loadu_si256((const __m256i*)(a.v + j)), x1 = _mm256_loadu_si256((const __m256i*)(a.v + j + 16));
        __m256i y0 = _mm256_loadu_si256((const __m256i*)(a.v + L1H + j));
        __m256i y1 = _mm256_loadu_si256((const __m256i*)(a.v + L1H + j + 16));
        x0 = _mm256_slli_epi16(_mm256_min_epi16(_mm256_max_epi16(x0, zero), top), 7);
        x1 = _mm256_slli_epi16(_mm256_min_epi16(_mm256_max_epi16(x1, zero), top), 7);
        y0 = _mm256_min_epi16(y0, top);
        y1 = _mm256_min_epi16(y1, top);
        const __m256i p = _mm256_packus_epi16(_mm256_mulhi_epi16(x0, y0), _mm256_mulhi_epi16(x1, y1));
        _mm256_store_si256((__m256i*)(out + j), p);
        const unsigned m =
          unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(p, zero)))) ^ 0xFFu;
        const __m256i  pos = _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(g_nzIdx[m])));
        _mm256_storeu_si256((__m256i*)(nz + n), _mm256_add_epi32(pos, _mm256_set1_epi32(firstGroup + j / 4)));
        n += CN_POPCNT(m);
    }
    return n;
}

// Activations of 32 int32 values (four registers) to 32 bytes in order: packs/packus work per 128-bit lane, the
// final permutation puts the 4-byte groups back in order.
inline __m256i act32(const __m256i* x, int cut, int sqShift, int vShift, bool square) {
    const __m256i zero = _mm256_setzero_si256(), top = _mm256_set1_epi32(127), c = _mm256_set1_epi32(cut);
    __m256i       r[4];
    for (int k = 0; k < 4; k++) {
        if (square) {
            const __m256i ax = _mm256_min_epi32(_mm256_abs_epi32(x[k]), c);
            r[k] = _mm256_min_epi32(_mm256_srli_epi32(_mm256_mullo_epi32(ax, ax), sqShift), top);
        } else
            r[k] = _mm256_min_epi32(_mm256_max_epi32(_mm256_srai_epi32(x[k], vShift), zero), top);
    }
    const __m256i b = _mm256_packus_epi16(_mm256_packs_epi32(r[0], r[1]), _mm256_packs_epi32(r[2], r[3]));
    return _mm256_permutevar8x32_epi32(b, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7));
}

Out forward_tail_avx2(const Stack& st, const __m256i* x1, const Acc& us, const Acc& them, int bucket) {
    alignas(32) std::uint8_t a1[2 * L2];
    _mm256_store_si256((__m256i*)a1, act32(x1, 16383, 21, 7, true));
    _mm256_store_si256((__m256i*)(a1 + 32), act32(x1, 16383, 21, 7, false));
    const std::int32_t* in = reinterpret_cast<const std::int32_t*>(a1);
    __m256i x2[4];
    for (int k = 0; k < 4; k++) x2[k] = _mm256_loadu_si256((const __m256i*)(st.b2 + 8 * k));
    for (int g = 0; g < 2 * L2 / 4; g++) {
        const __m256i  x = _mm256_set1_epi32(in[g]);
        const __m256i* w = reinterpret_cast<const __m256i*>(st.w2g[g]);
        for (int k = 0; k < 4; k++) x2[k] = dot8(x2[k], x, _mm256_load_si256(w + k));
    }
    alignas(32) std::uint8_t a2[2 * L3];
    _mm256_store_si256((__m256i*)a2, act32(x2, 8191, 19, 6, true));
    _mm256_store_si256((__m256i*)(a2 + 32), act32(x2, 8191, 19, 6, false));
    __m256i o = _mm256_setzero_si256();
    for (int k = 0; k < 2; k++) {
        o = dot8(o, _mm256_load_si256((const __m256i*)(a1 + 32 * k)), _mm256_loadu_si256((const __m256i*)(st.w3 + 32 * k)));
        o = dot8(o, _mm256_load_si256((const __m256i*)(a2 + 32 * k)),
                 _mm256_loadu_si256((const __m256i*)(st.w3 + 2 * L2 + 32 * k)));
    }
    __m128i s = _mm_add_epi32(_mm256_castsi256_si128(o), _mm256_extracti128_si256(o, 1));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0x4E));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0xB1));
    alignas(32) std::int32_t x1s[L2];
    _mm256_store_si256((__m256i*)(x1s + 24), x1[3]);
    const std::int32_t out  = st.b3 + _mm_cvtsi128_si32(s);
    const std::int32_t skip = x1s[L2 - 2] - x1s[L2 - 1];
    Out r;
    r.positional = int((std::int64_t(out) + skip) * 600 / 16384);
    r.psqt       = (us.psqt[bucket] - them.psqt[bucket]) / 32;
    return r;
}

Out forward_simd(const Net& net, const Acc& us, const Acc& them, int bucket CN_PK_PARAM) {
    CN_TIME(4);
    alignas(64) std::uint8_t l0[L1];
    alignas(64) std::int32_t nz[L1 / 4 + 8];
    int n = l0_side(us, l0, nz, 0, 0);
    n     = l0_side(them, l0 + L1H, nz, n, L1H / 4);
    const Stack&        st = net.stack[bucket];
    const std::int32_t* in = reinterpret_cast<const std::int32_t*>(l0);
    // Two chains of four accumulators (outputs 0-7, 8-15, 16-23, 24-31); the bias starts in the first chain.
    __m256i a[4], b[4];
    for (int k = 0; k < 4; k++) {
        a[k] = _mm256_loadu_si256((const __m256i*)(st.b1 + 8 * k));
#ifdef TRIUMV_C2
        a[k] = _mm256_add_epi32(a[k], _mm256_loadu_si256((const __m256i*)(pk + 8 * k)));
#endif
        b[k] = _mm256_setzero_si256();
    }
    int i = 0;
#if CN_DOT_CHAINS >= 3 && defined(USE_AVXVNNI)
    // Three independent chains with AVX-VNNI (vpdpbusd has a latency of about five cycles); same integer sums.
    __m256i c[4];
    for (int k = 0; k < 4; k++) c[k] = _mm256_setzero_si256();
    for (; i + 2 < n; i += 3) {
        const __m256i  x0 = _mm256_set1_epi32(in[nz[i]]), x1 = _mm256_set1_epi32(in[nz[i + 1]]);
        const __m256i  x2 = _mm256_set1_epi32(in[nz[i + 2]]);
        const __m256i* w0 = reinterpret_cast<const __m256i*>(st.w1g[nz[i]]);
        const __m256i* w1 = reinterpret_cast<const __m256i*>(st.w1g[nz[i + 1]]);
        const __m256i* w2 = reinterpret_cast<const __m256i*>(st.w1g[nz[i + 2]]);
        for (int k = 0; k < 4; k++) {
            a[k] = dot8(a[k], x0, _mm256_load_si256(w0 + k));
            b[k] = dot8(b[k], x1, _mm256_load_si256(w1 + k));
            c[k] = dot8(c[k], x2, _mm256_load_si256(w2 + k));
        }
    }
    for (int k = 0; k < 4; k++) a[k] = _mm256_add_epi32(a[k], c[k]);
#endif
    for (; i + 1 < n; i += 2) {
        const __m256i  x0 = _mm256_set1_epi32(in[nz[i]]), x1 = _mm256_set1_epi32(in[nz[i + 1]]);
        const __m256i* w0 = reinterpret_cast<const __m256i*>(st.w1g[nz[i]]);
        const __m256i* w1 = reinterpret_cast<const __m256i*>(st.w1g[nz[i + 1]]);
        for (int k = 0; k < 4; k++) {
            a[k] = dot8(a[k], x0, _mm256_load_si256(w0 + k));
            b[k] = dot8(b[k], x1, _mm256_load_si256(w1 + k));
        }
    }
    if (i < n) {
        const __m256i  x = _mm256_set1_epi32(in[nz[i]]);
        const __m256i* w = reinterpret_cast<const __m256i*>(st.w1g[nz[i]]);
        for (int k = 0; k < 4; k++) a[k] = dot8(a[k], x, _mm256_load_si256(w + k));
    }
    __m256i x1[4];
    for (int k = 0; k < 4; k++) x1[k] = _mm256_add_epi32(a[k], b[k]);
#ifdef TRIUMV_C2
    if (x1out)
        for (int k = 0; k < 4; k++) _mm256_storeu_si256((__m256i*)(x1out + 8 * k), x1[k]);
#endif
    return forward_tail_avx2(st, x1, us, them, bucket);
}
#else
Out forward_simd(const Net& net, const Acc& us, const Acc& them, int bucket CN_PK_PARAM) {
#ifdef TRIUMV_C2
    return forward_scalar(net, us, them, bucket, pk, x1out);
#else
    return forward_scalar(net, us, them, bucket);
#endif
}
#endif

#ifdef CN_SCALAR
    #define CN_FORWARD forward_scalar
#else
    #define CN_FORWARD forward_simd
#endif

// The 4-byte group of l0 (in the order of the specification) held at position g of the l0 buffer of forward_simd.
// AVX-512: each 64-byte block of one side holds, per 128-bit lane L, outputs 8L..8L+7 then 32+8L..32+8L+7.
// AVX2: each 32-byte block holds, per lane L, outputs 8L..8L+7 then 16+8L..16+8L+7.
inline int l0_group(int g) {
#if defined(USE_AVX512)
    const int side = g / (L1H / 4), blk = (g % (L1H / 4)) / 16, q = 4 * (g % 16);
    const int lane = q / 16, r = q % 16;
    const int out  = side * L1H + 64 * blk + (r < 8 ? 8 * lane + r : 32 + 8 * lane + r - 8);
    return out / 4;
#elif defined(USE_AVX2)
    const int side = g / (L1H / 4), blk = (g % (L1H / 4)) / 8, q = 4 * (g % 8);
    const int lane = q / 16, r = q % 16;
    const int out  = side * L1H + 32 * blk + (r < 8 ? 8 * lane + r : 16 + 8 * lane + r - 8);
    return out / 4;
#else
    return g;
#endif
}

inline int bucket_of(const Board& b) {
    return g_bucket_override >= 0 ? g_bucket_override : (CN_POPCNT(b.occ[2]) - 1) / 4;
}

#ifdef TRIUMV_C2
// ================================================================ pawn-king subnetwork (Consilium-2)
//
// Inputs from the side to move's view, squares as the trainer numbers them (a1 = 0) and flipped vertically when Black
// moves: engine square e (a8 = 0) -> e ^ 56 for White, e for Black. 0-63 own pawns, 64-127 enemy pawns, 128-191 own
// king, 192-255 enemy king. h = clamp(b1 + sum of W1 rows, 0, 127); out = b2 + W2 h, in the units of the l1 sums.
// The trainer keeps the weights on this integer grid, so the result is the trainer's exactly.
//
// SIMD (21/10/2026; a miss cost ~2,900 cycles with the plain loops, 3% of the evaluation time): the hidden layer in
// registers, its bytes put back in natural order after packus (which interleaves the 128-bit lanes), the output with
// the forward's broadcast of 4 inputs against pkW2g. Integer sums of the same terms: the result is unchanged (the
// u8 x s8 pairs cannot saturate: h <= 127, |W2| <= 127). -DCN_PK_SCALAR keeps the plain loops, the reference.
void pawn_king_compute(const Net& net, const Board& b, std::int32_t* out) {
    CN_TIME(5);
    const int us = b.stm, flip = us ? 0 : 56;
    const u64 own = b.bb[PAWN + 6 * us], opp = b.bb[PAWN + 6 * (us ^ 1)];
    const int k0 = 128 + (CN_LSB(b.bb[KING + 6 * us]) ^ flip), k1 = 192 + (CN_LSB(b.bb[KING + 6 * (us ^ 1)]) ^ flip);
#if defined(USE_AVX512) && !defined(CN_PK_SCALAR)
    constexpr int R = PK_H / 32;
    __m512i       h[R];
    for (int k = 0; k < R; k++) h[k] = _mm512_load_si512(net.pkB1 + 32 * k);
    auto add = [&](int idx) {
        const std::int16_t* w = net.pkW1[idx];
        for (int k = 0; k < R; k++) h[k] = _mm512_add_epi16(h[k], _mm512_load_si512(w + 32 * k));
    };
    for (u64 x = own; x; x &= x - 1) add(0 + (CN_LSB(x) ^ flip));
    for (u64 x = opp; x; x &= x - 1) add(64 + (CN_LSB(x) ^ flip));
    add(k0);
    add(k1);
    alignas(64) std::uint8_t a[PK_H];
    const __m512i zero = _mm512_setzero_si512(), top = _mm512_set1_epi16(127);
    const __m512i order = _mm512_set_epi64(7, 5, 3, 1, 6, 4, 2, 0);
    for (int k = 0; k < R / 2; k++) {
        const __m512i x0 = _mm512_min_epi16(_mm512_max_epi16(h[2 * k], zero), top);
        const __m512i x1 = _mm512_min_epi16(_mm512_max_epi16(h[2 * k + 1], zero), top);
        _mm512_store_si512(a + 64 * k, _mm512_permutexvar_epi64(order, _mm512_packus_epi16(x0, x1)));
    }
    // Four independent chains per half: one chain of 64 dependent products waited on the latency of each one.
    const std::int32_t* in = reinterpret_cast<const std::int32_t*>(a);
    __m512i c0[4], c1[4];
    for (int c = 0; c < 4; c++) c0[c] = c1[c] = _mm512_setzero_si512();
    for (int g = 0; g < PK_H / 4; g += 4)
        for (int c = 0; c < 4; c++) {
            const __m512i  x = _mm512_set1_epi32(in[g + c]);
            const __m512i* w = reinterpret_cast<const __m512i*>(net.pkW2g[g + c]);
            c0[c] = dot4(c0[c], x, _mm512_load_si512(w));
            c1[c] = dot4(c1[c], x, _mm512_load_si512(w + 1));
        }
    const __m512i o0 = _mm512_add_epi32(_mm512_load_si512(net.pkB2),
                                        _mm512_add_epi32(_mm512_add_epi32(c0[0], c0[1]), _mm512_add_epi32(c0[2], c0[3])));
    const __m512i o1 = _mm512_add_epi32(_mm512_load_si512(net.pkB2 + 16),
                                        _mm512_add_epi32(_mm512_add_epi32(c1[0], c1[1]), _mm512_add_epi32(c1[2], c1[3])));
    _mm512_storeu_si512(out, o0);
    _mm512_storeu_si512(out + 16, o1);
#elif defined(USE_AVX2) && !defined(CN_PK_SCALAR)
    constexpr int R = PK_H / 16;
    __m256i       h[R];
    for (int k = 0; k < R; k++) h[k] = _mm256_load_si256(reinterpret_cast<const __m256i*>(net.pkB1 + 16 * k));
    auto add = [&](int idx) {
        const std::int16_t* w = net.pkW1[idx];
        for (int k = 0; k < R; k++)
            h[k] = _mm256_add_epi16(h[k], _mm256_load_si256(reinterpret_cast<const __m256i*>(w + 16 * k)));
    };
    for (u64 x = own; x; x &= x - 1) add(0 + (CN_LSB(x) ^ flip));
    for (u64 x = opp; x; x &= x - 1) add(64 + (CN_LSB(x) ^ flip));
    add(k0);
    add(k1);
    alignas(64) std::uint8_t a[PK_H];
    const __m256i zero = _mm256_setzero_si256(), top = _mm256_set1_epi16(127);
    for (int k = 0; k < R / 2; k++) {
        const __m256i x0 = _mm256_min_epi16(_mm256_max_epi16(h[2 * k], zero), top);
        const __m256i x1 = _mm256_min_epi16(_mm256_max_epi16(h[2 * k + 1], zero), top);
        _mm256_store_si256(reinterpret_cast<__m256i*>(a + 32 * k),
                           _mm256_permute4x64_epi64(_mm256_packus_epi16(x0, x1), 0xD8));
    }
    // Two chains per quarter (8 accumulators), as on AVX-512.
    const std::int32_t* in = reinterpret_cast<const std::int32_t*>(a);
    __m256i o[2][4];
    for (int k = 0; k < 4; k++) o[0][k] = o[1][k] = _mm256_setzero_si256();
    for (int g = 0; g < PK_H / 4; g += 2)
        for (int c = 0; c < 2; c++) {
            const __m256i  x = _mm256_set1_epi32(in[g + c]);
            const __m256i* w = reinterpret_cast<const __m256i*>(net.pkW2g[g + c]);
            for (int k = 0; k < 4; k++) o[c][k] = dot8(o[c][k], x, _mm256_load_si256(w + k));
        }
    for (int k = 0; k < 4; k++)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 8 * k),
                            _mm256_add_epi32(_mm256_load_si256(reinterpret_cast<const __m256i*>(net.pkB2 + 8 * k)),
                                             _mm256_add_epi32(o[0][k], o[1][k])));
#else
    alignas(64) std::int16_t h[PK_H];
    std::memcpy(h, net.pkB1, sizeof(h));
    auto add = [&](int idx) {
        const std::int16_t* w = net.pkW1[idx];
        for (int j = 0; j < PK_H; j++) h[j] = std::int16_t(h[j] + w[j]);
    };
    for (u64 x = own; x; x &= x - 1) add(0 + (CN_LSB(x) ^ flip));
    for (u64 x = opp; x; x &= x - 1) add(64 + (CN_LSB(x) ^ flip));
    add(k0);
    add(k1);
    alignas(64) std::uint8_t a[PK_H];
    for (int j = 0; j < PK_H; j++) a[j] = std::uint8_t(h[j] < 0 ? 0 : h[j] > 127 ? 127 : h[j]);
    for (int o = 0; o < L2; o++) {
        std::int32_t s = net.pkB2[o];
        for (int j = 0; j < PK_H; j++) s += std::int32_t(a[j]) * net.pkW2[o][j];
        out[o] = s;
    }
#endif
}

    #ifdef CN_PKSTATS
struct PkStats {
    unsigned long long hit = 0, miss = 0;
    ~PkStats() {
        if (hit + miss)
            std::fprintf(stderr, "cn pawn-king cache: %llu hits, %llu misses (%.2f%% hits)\n", hit, miss,
                         100.0 * hit / double(hit + miss));
    }
};
PkStats g_pkStats;
        #define CN_PK_COUNT(f) (g_pkStats.f++)
    #else
        #define CN_PK_COUNT(f) ((void)0)
    #endif

// Cache key: the pawns of both colours, the king squares and the side to move (bit 15 marks a valid entry).
inline std::uint32_t pk_kings(const Board& b) {
    return std::uint32_t(CN_LSB(b.bb[KING])) | std::uint32_t(CN_LSB(b.bb[KING + 6])) << 6 | std::uint32_t(b.stm) << 12
         | 1u << 15;
}
inline std::size_t pk_index(const Board& b) {
    const u64 key = (b.bb[PAWN] * 0x9E3779B97F4A7C15ULL) ^ (b.bb[PAWN + 6] * 0xC2B2AE3D27D4EB4FULL)
                  ^ (u64(pk_kings(b)) * 0x165667B19E3779F9ULL);
    return std::size_t((key ^ (key >> 29)) & (PK_CACHE - 1));
}

// The subnetwork's outputs for the board, from the thread's cache (exact key) or computed and stored.
const std::int32_t* pawn_king(const Net& net, Handle& h, const Board& b) {
#ifdef CN_C2_PK_OFF
    // Cost diagnosis only (21/10/2026): the subnetwork contributes nothing and costs nothing.
    (void)net, (void)h, (void)b;
    alignas(64) static const std::int32_t zero[L2] = {};
    return zero;
#endif
    if (h.pkNet != net.contentHash) {
        std::memset(static_cast<void*>(h.pkc), 0, sizeof(h.pkc));
        h.pkNet = net.contentHash;
    }
    const u64           wp = b.bb[PAWN], bp = b.bb[PAWN + 6];
    const std::uint32_t kk = pk_kings(b);
    PkEntry&            e  = h.pkc[pk_index(b)];
    if (e.kk == kk && e.wp == wp && e.bp == bp) {
        CN_PK_COUNT(hit);
        return e.out;
    }
    CN_PK_COUNT(miss);
    pawn_king_compute(net, b, e.out);
    e.wp = wp;
    e.bp = bp;
    e.kk = kk;
    return e.out;
}
#endif

// ================================================================ file (spec 1)

struct Reader {
    const unsigned char* d;
    std::size_t          n, p = 0;
    bool                 ok = true;
    std::uint32_t        u32() {
        if (p + 4 > n) { ok = false; return 0; }
        std::uint32_t v = std::uint32_t(d[p]) | std::uint32_t(d[p + 1]) << 8 | std::uint32_t(d[p + 2]) << 16
                        | std::uint32_t(d[p + 3]) << 24;
        p += 4;
        return v;
    }
    // A tensor of `count` values of `bytes` bytes each (1, 2 or 4, signed), raw or LEB128-compressed.
    template <typename V>
    bool tensor(V* out, std::size_t count) {
        return tensor_f<V>(count, [out](std::size_t k, V v) { out[k] = v; });
    }
    template <typename V, typename F>
    bool tensor_f(std::size_t count, F put) {
        static const char magic[] = "COMPRESSED_LEB128";
        const std::size_t ml = sizeof(magic) - 1;
        if (p + ml <= n && std::memcmp(d + p, magic, ml) == 0) {
            p += ml;
            const std::size_t len = u32();
            if (!ok || p + len > n) return ok = false;
            std::size_t q = p, k = 0;
            const std::size_t end = p + len;
            while (k < count && q < end) {
                std::int64_t r = 0;
                int          shift = 0;
                std::uint8_t byte;
                do {
                    byte = d[q++];
                    r |= std::int64_t(byte & 0x7F) << shift;
                    shift += 7;
                } while ((byte & 0x80) && q < end && shift < 63);
                if (byte & 0x80) return ok = false;
                if (byte & 0x40) r |= -(std::int64_t(1) << shift);
                if (r < std::int64_t(std::numeric_limits<V>::min()) || r > std::int64_t(std::numeric_limits<V>::max()))
                    return ok = false;
                put(k, V(r));
                k++;
            }
            if (k != count || q != end) return ok = false;
            p = end;
            return true;
        }
        if (p + count * sizeof(V) > n) return ok = false;
        for (std::size_t k = 0; k < count; k++) {
            std::uint32_t v = 0;
            for (std::size_t j = 0; j < sizeof(V); j++) v |= std::uint32_t(d[p + k * sizeof(V) + j]) << (8 * j);
            put(k, V(v));
        }
        p += count * sizeof(V);
        return true;
    }
};

constexpr std::uint32_t VERSION = 0x6A448AFA;

std::uint32_t feature_hash() {
#ifdef TRIUMV_C2
    // Full_Threats, HalfKAv2_hm_X8 ("X8C1"), PP_3Wide, PassedPawns (trainer: model/modules/features/*.py HASH).
    const std::uint32_t blocks[4] = {0x2E6B9D04u, 0x7F234CB8u ^ 0x58384331u, 0x86F2B1DDu, 0x50535344u};
#else
    const std::uint32_t blocks[4] = {0x2E6B9D04u, 0x7F234CB8u ^ 0x50484134u, 0x86F2B1DDu, 0x50535344u};
#endif
    std::uint32_t h = 0;
    for (std::uint32_t bh : blocks) h = ((h << 1) | (h >> 31)) ^ bh;
    return h;
}
std::uint32_t fc_hash() {
    std::uint32_t prev = 0xEC42E90Du ^ std::uint32_t(L1 * 2);
    const int outs[3] = {L2, L3, 1};
    for (int o : outs) {
        std::uint32_t h = 0xCC03DAE4u + std::uint32_t(o);
        h ^= prev >> 1;
        h ^= prev << 31;
        if (o != 1) h += 0x538D24C7u;
        prev = h;
    }
    return prev;
}

}  // namespace

// ================================================================ public

#ifdef TRIUMV_C2
// --- C2 signals for the search (agent A) ---
int pk_skip(const Net& net, Handle& h, const Board& b) {
    const std::int32_t* pk = pawn_king(net, h, b);
    return int((std::int64_t(pk[L2 - 2]) - pk[L2 - 1]) * 600 / 16384);
}
#endif

void init() {
    build_tables();
    build_fast_threats();
    build_thr_slots();
    if (!check_fast_threats()) std::fprintf(stderr, "cn: fast threat tables differ from the specification\n");
}

bool load_memory(const unsigned char* data, std::size_t size, Net* net, std::string* err) {
    Reader r{data, size};
    const std::uint32_t fh = feature_hash(), ch = fc_hash();
    auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    if (r.u32() != VERSION) return fail("version");
    if (r.u32() != (ch ^ fh ^ std::uint32_t(L1 * 2))) return fail("network hash (not a Consilium net)");
    const std::uint32_t dl = r.u32();
    if (!r.ok || r.p + dl > size) return fail("description");
    r.p += dl;
    if (r.u32() != (fh ^ std::uint32_t(L1 * 2))) return fail("feature transformer hash");
    bool ok = r.tensor(net->ftBias, L1);
    // Weights row by row into the strided rows (row `slot(file row)`), then the PSQT values into the tail of each row.
    // CN_W1: element e of an int8 row goes to its interleaved byte (block of B = CN_W1_BLOCK bytes, 32 on AVX2 and 64
    // on AVX-512: element Bb + j -> byte Bb + 2j, element Bb + B/2 + j -> byte Bb + 2j + 1); the PSQT tail after the
    // 1024 weights is not touched.
    auto block = [&](auto file_type_tag, auto* rows, int nrows, auto slot) {
        using F = decltype(file_type_tag);
        auto pos = [](std::size_t e) -> std::size_t {
#ifdef CN_W1
            if (sizeof(F) == 1) {
                constexpr std::size_t B = CN_W1_BLOCK, H = B / 2;
                const std::size_t     l = e % B;
                return e - l + (l < H ? 2 * l : 2 * (l - H) + 1);
            }
#endif
            return e;
        };
        bool ok2 = r.template tensor_f<F>(std::size_t(nrows) * L1, [rows, slot, pos](std::size_t k, F v) {
            rows[slot(int(k / L1))][pos(k % L1)] = v;
        });
        return ok2 && r.template tensor_f<std::int32_t>(std::size_t(nrows) * NPSQT, [rows, slot](std::size_t k, std::int32_t v) {
            std::memcpy(reinterpret_cast<char*>(rows[slot(int(k / NPSQT))] + L1) + 4 * (k % NPSQT), &v, 4);
        });
    };
    auto same = [](int row) { return row; };
#ifdef TRIUMV_C2
    using HkaFile = std::int8_t;   // Consilium-2: HalfKA exported in int8 (raw), like the threat rows
#else
    using HkaFile = std::int16_t;
#endif
    ok = ok && block(std::int8_t{}, net->wThr, ROWS_THR, [](int row) { return thr_slot(row); })
            && block(HkaFile{}, net->wHka, ROWS_HKA, same) && block(std::int8_t{}, net->wPP, ROWS_PP, same)
            && block(std::int8_t{}, net->wPas, ROWS_PAS, same);
    if (!ok) return fail("feature transformer tensors");
    for (int b = 0; b < NBUCKETS; b++) {
        Stack& s = net->stack[b];
        if (r.u32() != ch) return fail("layer stack hash");
        ok = r.tensor(s.b1, L2) && r.tensor(&s.w1[0][0], std::size_t(L2) * L1) && r.tensor(s.b2, L3)
          && r.tensor(&s.w2[0][0], std::size_t(L3) * 2 * L2) && r.tensor(&s.b3, 1)
          && r.tensor(s.w3, std::size_t(2 * L2 + 2 * L3));
        if (!ok) return fail("layer stack tensors");
        for (int g = 0; g < L1 / 4; g++)
            for (int o = 0; o < L2; o++)
                for (int k = 0; k < 4; k++) s.w1g[g][o][k] = s.w1[o][4 * l0_group(g) + k];
        for (int g = 0; g < 2 * L2 / 4; g++)
            for (int o = 0; o < L3; o++)
                for (int k = 0; k < 4; k++) s.w2g[g][o][k] = s.w2[o][4 * g + k];
    }
#ifdef TRIUMV_C2
    // Section RTB1: the routing table (cell -> expert). Section PKN1: the pawn-king subnetwork.
    if (r.u32() != 0x52544231u) return fail("router section (RTB1)");
    if (r.u32() != std::uint32_t(ROUTER_CELLS) || r.u32() != std::uint32_t(NEXPERTS)) return fail("router size");
    ok = r.tensor(net->router, ROUTER_CELLS);
    for (int c = 0; ok && c < ROUTER_CELLS; c++) ok = net->router[c] < NEXPERTS;
    if (!ok) return fail("router table");
    if (r.u32() != 0x504B4E31u) return fail("pawn-king section (PKN1)");
    if (r.u32() != std::uint32_t(PK_H) || r.u32() != std::uint32_t(L2)) return fail("pawn-king size");
    ok = r.tensor(net->pkB1, PK_H) && r.tensor(&net->pkW1[0][0], std::size_t(PK_INPUTS) * PK_H)
      && r.tensor(net->pkB2, L2) && r.tensor(&net->pkW2[0][0], std::size_t(L2) * PK_H);
    if (!ok) return fail("pawn-king tensors");
    for (int g = 0; g < PK_H / 4; g++)
        for (int o = 0; o < L2; o++)
            for (int k = 0; k < 4; k++) net->pkW2g[g][o][k] = net->pkW2[o][4 * g + k];
#endif
    if (r.p != size) return fail("trailing bytes");
#ifdef TRIUMV_C2
    std::memcpy(g_router, net->router, sizeof(g_router));   // the make and Persp read this copy
    pk_measure(*net);                                        // C2 signals (agent B): ranks and medians of pk outputs
#endif
    // Name of the shared-memory copy: FNV-1a over 8-byte words (it only has to tell networks apart), plus the tail.
    std::uint64_t h = 1469598103934665603ULL ^ std::uint64_t(size);
    std::size_t   i = 0;
    for (; i + 8 <= size; i += 8) {
        std::uint64_t w;
        std::memcpy(&w, data + i, 8);
        h = (h ^ w) * 1099511628211ULL;
    }
    for (; i < size; i++) h = (h ^ data[i]) * 1099511628211ULL;
    // The order of w1g depends on the instruction set (l0_group): builds for different sets must not share a copy.
    std::uint64_t layout = 0;   // differs between the AVX-512, AVX2 and plain orders of w1g
    for (int g = 0; g < L1 / 4; g++) layout = layout * 1099511628211ULL + std::uint64_t(l0_group(g));
    h ^= layout;
#ifdef CN_W1
    h ^= 0x5731'0000'0000'0000ULL;   // interleaved int8 rows: another layout of the same file
#endif
#ifdef CN_THRPERM
    h ^= THR_PERM_ID;   // and so does the order of the threat rows
#endif
    net->contentHash = h | 1;   // never 0: 0 marks an empty cache in the handle
    return true;
}

bool load_file(const char* path, Net* net, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open file";
        return false;
    }
    // One read of the whole file (the byte iterator was ~2% of the engine's instructions at startup).
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size <= 0) {
        if (err) *err = "empty file";
        return false;
    }
    std::vector<unsigned char> buf(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(buf.data()), size)) {
        if (err) *err = "read error";
        return false;
    }
    return load_memory(buf.data(), buf.size(), net, err);
}

void reset(Handle& h) {
    NnStack& S = h.stack;
    S.size = 1;
    S.ready = 1;
    S.st[0].computed[0] = S.st[0].computed[1] = 0;
    S.st[0].idx = 0;
    S.st[0].threats.n = 0;
    S.v1PassW[0] = S.v1PassB[0] = ~0ULL;
}

// The HalfKA rows a move changes for both perspectives (the piece leaving, the piece arriving, the captured piece),
// first line and PSQT line of each, requested from the make (19/10/2026): the hash probe and the rest of the child
// node cover the memory latency before its evaluation reads them (push16 asks for the same lines, then already in
// cache). Without a capture the captured row is the leaving one again, so nothing depends on a branch. A king move
// refreshes its own perspective from the cache instead, and the rows asked for there go unused. Only a hint: no
// state is read or written besides the king squares. -DCN_NO_MAKE_PREFETCH turns it off.
void prefetch_move(const Net& net, const std::uint64_t* bb, int phase, int pc, int from, int toPc, int to, int capPc,
                   int capSq) {
#ifndef CN_NO_MAKE_PREFETCH
    for (int p = 0; p < 2; p++) {
        const int           ksq  = CN_LSB(bb[KING + 6 * p]);
#ifdef TRIUMV_C2
        const int           base = ((phase >> (4 * p)) & 15) * HKA_PHASE_ROWS + g_kb[p][ksq];
        using Row = std::int8_t;
#else
        const int           base = phase * HKA_PHASE_ROWS + g_kb[p][ksq];
        using Row = std::int16_t;
#endif
        const std::int16_t (*hk)[64] = g_hk[p][mirror_hka(ksq)];
        const Row* r0 = net.wHka[base + hk[pc][from]];
        const Row* r1 = net.wHka[base + hk[toPc][to]];
        const Row* r2 = net.wHka[base + hk[capPc][capSq]];
        CN_PREFETCH(r0);
        CN_PREFETCH(r0 + L1);
        CN_PREFETCH(r1);
        CN_PREFETCH(r1 + L1);
        CN_PREFETCH(r2);
        CN_PREFETCH(r2 + L1);
    }
#else
    (void)net, (void)bb, (void)phase, (void)pc, (void)from, (void)toPc, (void)to, (void)capPc, (void)capSq;
#endif
}

Out evaluate_refresh(const Net& net, const Board& b, Acc acc[2]) {
    refresh_one(net, b, 0, acc[0]);
    refresh_one(net, b, 1, acc[1]);
#ifdef TRIUMV_C2
    alignas(64) std::int32_t pk[L2];
    pawn_king_compute(net, b, pk);
#endif
    return CN_FORWARD(net, acc[b.stm], acc[b.stm ^ 1], bucket_of(b) CN_PK_ARG(pk));
}

namespace {

// Does move i force a refresh of perspective p? A king move of p (new bucket, maybe new mirror) or a phase change.
#ifdef TRIUMV_C2
// Consilium-2: the expert changes per perspective (phaseChanged bit p, written by the make).
inline bool barrier(const NnDirtyPiece& dp, int p) { return dp.pc == KING + 6 * p || ((dp.phaseChanged >> p) & 1); }
#else
inline bool barrier(const NnDirtyPiece& dp, int p) { return dp.pc == KING + 6 * p || dp.phaseChanged; }
#endif

// A refresh in one pass: the cache entry brought to the board (the HalfKA rows that differ, LH, written back to the
// entry), then the threat, pawn-pair and passed-pawn rows of the board (LO, all int8 and added) on top of it into the
// accumulator. As in apply, the first tile also sums the PSQT values of the rows.
#if defined(USE_AVX512)
inline __m256i psqt_of(const void* row, std::size_t bytes) {
    return _mm256_load_si256(reinterpret_cast<const __m256i*>(static_cast<const char*>(row) + bytes));
}

template <bool WithPsqt>
inline void refresh_tile(const RowList& LH, const RowList& LO, Acc& entry, Acc& dst, int t) {
    constexpr int R = 16, W = 32;
    __m512i       r[R];
    __m256i       ps = WithPsqt ? _mm256_loadu_si256((const __m256i*)entry.psqt) : _mm256_setzero_si256();
    for (int k = 0; k < R; k++) r[k] = _mm512_loadu_si512(entry.v + t + k * W);
    for (int i = 0; i < LH.n16a; i++) {
        for (int k = 0; k < R; k++) r[k] = _mm512_add_epi16(r[k], _mm512_load_si512(LH.a16[i] + t + k * W));
        if (WithPsqt) ps = _mm256_add_epi32(ps, psqt_of(LH.a16[i], 2 * L1));
    }
    for (int i = 0; i < LH.n16s; i++) {
        for (int k = 0; k < R; k++) r[k] = _mm512_sub_epi16(r[k], _mm512_load_si512(LH.s16[i] + t + k * W));
        if (WithPsqt) ps = _mm256_sub_epi32(ps, psqt_of(LH.s16[i], 2 * L1));
    }
#ifdef TRIUMV_C2
    // Consilium-2: the HalfKA rows are int8 (in the 8-bit lists of LH).
    rows8_512<R, WithPsqt>(LH, r, ps, t);
#endif
    for (int k = 0; k < R; k++) _mm512_storeu_si512(entry.v + t + k * W, r[k]);
    if (WithPsqt) _mm256_storeu_si256((__m256i*)entry.psqt, ps);
    rows8_512<R, WithPsqt>(LO, r, ps, t);   // added rows only (the board's): the removed list is empty
    for (int k = 0; k < R; k++) _mm512_storeu_si512(dst.v + t + k * W, r[k]);
    if (WithPsqt) _mm256_storeu_si256((__m256i*)dst.psqt, ps);
}

void apply_refresh(const RowList& LH, const RowList& LO, Acc& entry, Acc& dst) {
    CN_TIME(1);
    refresh_tile<true>(LH, LO, entry, dst, 0);
    refresh_tile<false>(LH, LO, entry, dst, L1 / 2);
}
#else
void apply_refresh(const RowList& LH, const RowList& LO, Acc& entry, Acc& dst) {
    apply(LH, entry, entry);
    apply(LO, entry, dst);
}
#endif


// One pass of a king step: S(parent) - entry of the parent's king square (brought to the parent board, LP) + entry of
// the child's king square (brought to the board, LN) + the O rows of the move (LO, int8). Both entries are written
// back. Four tiles of 8 registers (two accumulators live at once); the first one also carries the PSQT values.
#if defined(USE_AVX512)
template <bool WithPsqt>
inline void kingstep_tile(const RowList& LP, const RowList& LN, const RowList& LO, const Acc& sPar, Acc& ePar,
                          Acc& eNew, Acc& dst, int t) {
    constexpr int R = 8, W = 32;
    __m512i       sv[R], ev[R];
    __m256i       sp = _mm256_setzero_si256(), ep = _mm256_setzero_si256();
    if (WithPsqt) {
        sp = _mm256_loadu_si256((const __m256i*)sPar.psqt);
        ep = _mm256_loadu_si256((const __m256i*)ePar.psqt);
    }
    for (int k = 0; k < R; k++) {
        sv[k] = _mm512_loadu_si512(sPar.v + t + k * W);
        ev[k] = _mm512_loadu_si512(ePar.v + t + k * W);
    }
    for (int i = 0; i < LP.n16a; i++) {
        for (int k = 0; k < R; k++) ev[k] = _mm512_add_epi16(ev[k], _mm512_load_si512(LP.a16[i] + t + k * W));
        if (WithPsqt) ep = _mm256_add_epi32(ep, psqt_of(LP.a16[i], 2 * L1));
    }
    for (int i = 0; i < LP.n16s; i++) {
        for (int k = 0; k < R; k++) ev[k] = _mm512_sub_epi16(ev[k], _mm512_load_si512(LP.s16[i] + t + k * W));
        if (WithPsqt) ep = _mm256_sub_epi32(ep, psqt_of(LP.s16[i], 2 * L1));
    }
#ifdef TRIUMV_C2
    rows8_512<R, WithPsqt>(LP, ev, ep, t);
#endif
    for (int k = 0; k < R; k++) {
        _mm512_storeu_si512(ePar.v + t + k * W, ev[k]);
        sv[k] = _mm512_sub_epi16(sv[k], ev[k]);
        ev[k] = _mm512_loadu_si512(eNew.v + t + k * W);
    }
    if (WithPsqt) {
        _mm256_storeu_si256((__m256i*)ePar.psqt, ep);
        sp = _mm256_sub_epi32(sp, ep);
        ep = _mm256_loadu_si256((const __m256i*)eNew.psqt);
    }
    for (int i = 0; i < LN.n16a; i++) {
        for (int k = 0; k < R; k++) ev[k] = _mm512_add_epi16(ev[k], _mm512_load_si512(LN.a16[i] + t + k * W));
        if (WithPsqt) ep = _mm256_add_epi32(ep, psqt_of(LN.a16[i], 2 * L1));
    }
    for (int i = 0; i < LN.n16s; i++) {
        for (int k = 0; k < R; k++) ev[k] = _mm512_sub_epi16(ev[k], _mm512_load_si512(LN.s16[i] + t + k * W));
        if (WithPsqt) ep = _mm256_sub_epi32(ep, psqt_of(LN.s16[i], 2 * L1));
    }
#ifdef TRIUMV_C2
    rows8_512<R, WithPsqt>(LN, ev, ep, t);
#endif
    for (int k = 0; k < R; k++) {
        _mm512_storeu_si512(eNew.v + t + k * W, ev[k]);
        sv[k] = _mm512_add_epi16(sv[k], ev[k]);
    }
    if (WithPsqt) {
        _mm256_storeu_si256((__m256i*)eNew.psqt, ep);
        sp = _mm256_add_epi32(sp, ep);
    }
    rows8_512<R, WithPsqt>(LO, sv, sp, t);
    for (int k = 0; k < R; k++) _mm512_storeu_si512(dst.v + t + k * W, sv[k]);
    if (WithPsqt) _mm256_storeu_si256((__m256i*)dst.psqt, sp);
}

void apply_kingstep(const RowList& LP, const RowList& LN, const RowList& LO, const Acc& sPar, Acc& ePar, Acc& eNew,
                    Acc& dst) {
    CN_TIME(1);
    kingstep_tile<true>(LP, LN, LO, sPar, ePar, eNew, dst, 0);
    for (int t = 8 * 32; t < L1; t += 8 * 32) kingstep_tile<false>(LP, LN, LO, sPar, ePar, eNew, dst, t);
}
#else
void apply_kingstep(const RowList& LP, const RowList& LN, const RowList& LO, const Acc& sPar, Acc& ePar, Acc& eNew,
                    Acc& dst) {
    apply(LP, ePar, ePar);
    apply(LN, eNew, eNew);
    alignas(64) Acc t;
    for (int j = 0; j < L1; j++)
        t.v[j] = std::int16_t(std::uint16_t(sPar.v[j]) - std::uint16_t(ePar.v[j]) + std::uint16_t(eNew.v[j]));
    for (int k = 0; k < NPSQT; k++) t.psqt[k] = sPar.psqt[k] - ePar.psqt[k] + eNew.psqt[k];
    apply(LO, t, dst);
}
#endif

void move_rows_all(const NnStack& S, int i, const Persp& P, RowList& L, int dir) {
    move_rows_h(S, i, P, L, dir);
    move_rows_o(S, i, P, L, dir);
}

}  // namespace

Out evaluate(const Net& net, Handle& h, const Board& b) {
    CN_TIME(0);
#ifdef TRIUMV_C2
    {   // The pawn-king entry is read at the end: asked for now, so it arrives under the accumulator work (21/10/2026).
        const char* e = reinterpret_cast<const char*>(&h.pkc[pk_index(b)]);
        CN_PREFETCH(e);
        CN_PREFETCH(e + 64);
        CN_PREFETCH(e + 128);
    }
#endif
    NnStack&  S   = h.stack;
    const int top = S.size - 1;
    static_assert(3 * sizeof(RowList) <= sizeof(h.scratch), "Handle::scratch too small for 3 row lists");
    RowList* lists = reinterpret_cast<RowList*>(h.scratch);

    // For each perspective: the nearest computed state, unless a move on the way forces a refresh.
    int  from[2];
    bool refresh[2];
    for (int p = 0; p < 2; p++) {
        int  c  = top;
        bool rb = false;
        while (c > 0 && !(S.st[c].computed[p] & 1)) {
            if (barrier(S.st[c].dp, p)) {
                rb = true;
                break;
            }
            c--;
        }
        if (!rb && !(S.st[c].computed[p] & 1)) rb = true;   // the root, never computed
        from[p]    = c;
        refresh[p] = rb;
    }

    if (!refresh[0] && !refresh[1] && from[0] == top - 1 && from[1] == top - 1) {
        // The common case: one move from the parent, both perspectives; each threat tuple decoded once.
        const Persp    P0(0, b), P1(1, b);
        RowList*       L0    = new (&lists[0]) RowList(&net);
        RowList*       L1    = new (&lists[1]) RowList(&net);
        const Persp*   PP[2] = {&P0, &P1};
        RowList* const LL[2] = {L0, L1};
        move_rows_h(S, top, P0, *L0, 1);
        move_rows_h(S, top, P1, *L1, 1);
        move_threats(S, top, PP, LL, 1);
        move_pawns(S, top, P0, *L0, 1);
        move_pawns(S, top, P1, *L1, 1);
        apply(*L0, h.acc[top - 1][0], h.acc[top][0]);
        apply(*L1, h.acc[top - 1][1], h.acc[top][1]);
        S.st[top].computed[0] |= 1;
        S.st[top].computed[1] |= 1;
    } else {
        for (int p = 0; p < 2; p++) {
            if (S.st[top].computed[p] & 1) continue;
            const Persp P(p, b);
            const NnDirtyPiece& dpt = S.st[top].dp;
#ifndef CN_NO_KINGSTEP
            if (refresh[p] && from[p] == top && top > 0 && (S.st[top - 1].computed[p] & 1)
                && !(dpt.pc == KING + 6 * p && mirror_oth(dpt.from) != mirror_oth(dpt.to))) {
                // King step inside the same half, or a phase change, from a computed parent: no O recomputation.
                std::uint64_t bbp[12], occp[3];
                for (int pc = 0; pc < 12; pc++) bbp[pc] = b.bb[pc];
                bbp[dpt.pc] ^= 1ULL << dpt.from;
                if (dpt.to != NN_SQ_NONE) bbp[dpt.pc] ^= 1ULL << dpt.to;
                if (dpt.remove_sq != NN_SQ_NONE) bbp[dpt.remove_pc] ^= 1ULL << dpt.remove_sq;
                if (dpt.add_sq != NN_SQ_NONE) bbp[dpt.add_pc] ^= 1ULL << dpt.add_sq;
                occp[0] = bbp[0] | bbp[1] | bbp[2] | bbp[3] | bbp[4] | bbp[5];
                occp[1] = bbp[6] | bbp[7] | bbp[8] | bbp[9] | bbp[10] | bbp[11];
                occp[2] = occp[0] | occp[1];
                const Board  bpar{bbp, occp, b.mb, b.stm ^ 1};
                const Persp  Ppar(p, bpar);
                RowList*     LP   = new (&lists[0]) RowList(&net);
                RowList*     LN   = new (&lists[1]) RowList(&net);
                RowList*     LO   = new (&lists[2]) RowList(&net);
                KingCacheEntry&  ePar = kcache_rows(net, h, bpar, Ppar, *LP);
                KingCacheEntry&  eNew = kcache_rows(net, h, b, P, *LN);
                move_rows_o(S, top, P, *LO, 1);
                apply_kingstep(*LP, *LN, *LO, h.acc[top - 1][p], ePar.acc, eNew.acc, h.acc[top][p]);
                S.st[top].computed[p] |= 1;
                continue;
            }
#endif
            if (refresh[p]) {
                RowList*    LH = new (&lists[0]) RowList(&net);
                RowList*    LO = new (&lists[1]) RowList(&net);
                KingCacheEntry& e  = kcache_rows(net, h, b, P, *LH);
                active_other(b, P, *LO);
                apply_refresh(*LH, *LO, e.acc, h.acc[top][p]);
                // Back down the chain to the state of the refresh (after the king move or phase change, or the
                // root), taking each move back: the nearby branches then start from a computed state.
                for (int i = top; i > from[p]; i--) {
                    RowList* L = new (&lists[2]) RowList(&net);
                    move_rows_all(S, i, P, *L, -1);
                    apply(*L, h.acc[i][p], h.acc[i - 1][p]);
                    S.st[i - 1].computed[p] |= 1;
                }
            } else {
                // Every state of the chain, one move at a time.
                CN_COUNT(7, top - from[p]);
                for (int i = from[p] + 1; i <= top; i++) {
                    RowList* L = new (&lists[2]) RowList(&net);
                    move_rows_all(S, i, P, *L, 1);
                    apply(*L, h.acc[i - 1][p], h.acc[i][p]);
                    S.st[i].computed[p] |= 1;
                }
            }
            S.st[top].computed[p] |= 1;
        }
    }
#ifdef TRIUMV_C2
    const std::int32_t* pk = pawn_king(net, h, b);
    return CN_FORWARD(net, h.acc[top][b.stm], h.acc[top][b.stm ^ 1], bucket_of(b), pk, h.x1);   // x1 kept: pk_effect
#else
    return CN_FORWARD(net, h.acc[top][b.stm], h.acc[top][b.stm ^ 1], bucket_of(b));
#endif
}

#ifdef TRIUMV_C2
// --- C2 signals (agent B) ---
// Read-only views of the pawn-king subnetwork and of the routing for the engine's code outside the search loop
// (21/10/2026). Nothing here changes an evaluation.
//
// pk_measure, at load: the 32 outputs are computed on 1,024 game-like pawn-king placements drawn with a fixed seed
// (below), both sides to move; per output the median, and the outputs ranked by their mean distance from it.
// pk_signature keeps the signs against the medians of the `bits` outputs that vary most: the structures the
// subnetwork reads alike share a key. A function of the network's weights only: the same key on every machine.
// Bench 21/10/2026 (C2B_STATS): the 12-bit key takes 229 values over the 129,498 evaluations, bits on 20-82%.
namespace {
std::int32_t g_pk_med[L2];
std::uint8_t g_pk_rank[L2];
}  // namespace

void pk_measure(const Net& net) {
    constexpr int SAMPLES = 1024;
    std::vector<std::int32_t> v(std::size_t(SAMPLES) * L2);
    u64 s = 0x2545F4914F6CDD1DULL;
    auto rnd = [&s](int n) {   // xorshift64*: uniform enough for a placement
        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return int(((s * 0x2545F4914F6CDD1DULL) >> 33) % u64(n));
    };
    // Game-like placements: the pawns of the opening, each one removed with a probability drawn per sample (0-75%, so
    // the samples run from the opening to pawn endgames), the rest advanced 0-3 ranks (50/30/15/5%) and moved to a
    // neighbouring file one time in eight (a capture); kings on a castled or central first-rank square, or anywhere on
    // their half of the board one time in three (endgames). Engine squares: a8 = 0, rank r of file f = (8 - r) * 8 + f.
    static const int adv[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3};
    static const int kfile[7] = {6, 7, 2, 1, 4, 5, 3};   // g, h, c, b, e, f, d
    for (int i = 0; i < SAMPLES; i++) {
        std::uint64_t bb[12] = {};
        u64           occ = 0;
        const int     keep = 25 + rnd(76);   // percent of pawns kept
        for (int c = 0; c < 2; c++)
            for (int f = 0; f < 8; f++) {
                if (rnd(100) >= keep) continue;
                const int a = adv[rnd(20)], r = c == 0 ? 2 + a : 7 - a;
                const int ff = rnd(8) ? f : f == 0 ? 1 : f == 7 ? 6 : f + (rnd(2) ? 1 : -1);
                const int sq = (8 - r) * 8 + ff;
                if (!(occ >> sq & 1)) bb[PAWN + 6 * c] |= 1ULL << sq, occ |= 1ULL << sq;
            }
        for (int c = 0; c < 2; c++) {
            int sq;
            if (rnd(3))
                sq = (c == 0 ? 56 : 0) + kfile[rnd(7)];   // first rank: no pawn ever stands there
            else
                do sq = (c == 0 ? 32 : 0) + rnd(32);       // White: ranks 1-4, Black: ranks 5-8
                while (occ >> sq & 1);
            bb[KING + 6 * c] = 1ULL << sq, occ |= 1ULL << sq;
        }
        const Board b{bb, nullptr, nullptr, i & 1};
        pawn_king_compute(net, b, &v[std::size_t(i) * L2]);
    }
    std::int64_t spread[L2];
    for (int o = 0; o < L2; o++) {
        std::vector<std::int32_t> col(SAMPLES);
        for (int i = 0; i < SAMPLES; i++) col[i] = v[std::size_t(i) * L2 + o];
        std::nth_element(col.begin(), col.begin() + SAMPLES / 2, col.end());
        g_pk_med[o] = col[SAMPLES / 2];
        spread[o]   = 0;
        for (int i = 0; i < SAMPLES; i++) spread[o] += std::abs(std::int64_t(col[i]) - g_pk_med[o]);
        g_pk_rank[o] = std::uint8_t(o);
    }
    std::stable_sort(g_pk_rank, g_pk_rank + L2, [&](std::uint8_t a, std::uint8_t c) { return spread[a] > spread[c]; });
}

const std::int32_t* pk_outputs(const Net& net, Handle& h, const Board& b) { return pawn_king(net, h, b); }

// The positional output of the layers after l1 for given l1 sums, with the tail the evaluation uses (identical
// integers on every path). Only the positional part is read, so any accumulator serves for the psqt argument.
namespace {
inline int tail_positional(const Stack& st, const std::int32_t* x1, const Acc& any, int bucket) {
#if defined(USE_AVX512)
    return forward_tail_simd(st, x1, any, any, bucket).positional;
#elif defined(USE_AVX2)
    __m256i r[4];
    for (int k = 0; k < 4; k++) r[k] = _mm256_loadu_si256((const __m256i*)(x1 + 8 * k));
    return forward_tail_avx2(st, r, any, any, bucket).positional;
#else
    return forward_tail(st, x1, any, any, bucket).positional;
#endif
}
}  // namespace

// Measured 21/10/2026 on the bench (diagnostic build C2B_STATS): the share that reaches the output through the skip
// alone, (pk[30] - pk[31]) x 600 / 16384, is ~1% of |positional| (mean 4.9 against 415), so the subnetwork acts through
// the activations and only the difference of two tails measures it.
int pk_effect(const Net& net, Handle& h, const Board& b, int positional) {
    const std::int32_t*      pk = pawn_king(net, h, b);   // the entry evaluate just used: a hit
    alignas(64) std::int32_t x1[L2];
    for (int o = 0; o < L2; o++) x1[o] = h.x1[o] - pk[o];
    const int bucket = bucket_of(b);
    return positional - tail_positional(net.stack[bucket], x1, h.acc[0][0], bucket);
}

int pk_effect_refresh(const Net& net, const Board& b, const Acc acc[2]) {
    alignas(64) std::int32_t pk[L2], x1[L2];
    pawn_king_compute(net, b, pk);
    const int bucket     = bucket_of(b);
    const int positional = forward_scalar(net, acc[b.stm], acc[b.stm ^ 1], bucket, pk, x1).positional;
    for (int o = 0; o < L2; o++) x1[o] -= pk[o];
    return positional - tail_positional(net.stack[bucket], x1, acc[0], bucket);
}

std::int64_t pk_signature(const Net& net, Handle& h, const Board& b, int bits, bool compute) {
    const std::int32_t* out;
    if (compute)
        out = pawn_king(net, h, b);
    else {   // only an entry already in the cache, for this network: no subnetwork pass on a miss
        const PkEntry& e = h.pkc[pk_index(b)];
        if (h.pkNet != net.contentHash || e.kk != pk_kings(b) || e.wp != b.bb[PAWN] || e.bp != b.bb[PAWN + 6])
            return -1;
        out = e.out;
    }
    bits = bits < 1 ? 1 : bits > L2 ? L2 : bits;
    std::uint32_t sig = 0;
    for (int i = 0; i < bits; i++) sig |= std::uint32_t(out[g_pk_rank[i]] > g_pk_med[g_pk_rank[i]]) << i;
    return sig;
}

int expert_of_board(const Board& b, int p) { return expert_of(b, p); }
#endif

bool same_accumulators(const Handle& h, const Acc acc[2]) {
    const int top = h.stack.size - 1;
    for (int p = 0; p < 2; p++)
        if (std::memcmp(h.acc[top][p].v, acc[p].v, sizeof(acc[p].v))
            || std::memcmp(h.acc[top][p].psqt, acc[p].psqt, sizeof(acc[p].psqt)))
            return false;
    return true;
}

}  // namespace cn
