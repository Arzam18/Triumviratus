// Triumviratus, a UCI chess engine.
// Copyright (C) 2026 Francesco Torsello
// SPDX-License-Identifier: GPL-3.0-or-later (see COPYING)

// consilium_bridge.cpp -- the engine's link with the Consilium network (block 6 of 8.1, 18/10/2026).
//
// The network is evaluated by the module in consilium/ (cn.h, cn.cpp), written from our own specification of the
// network (docs/development/8.1/CONSILIUM_INFERENCE.md); the previous inference is gone. This file holds what the
// engine adds around the module:
//   - loading the network (file, embedded resource, memory shared between processes);
//   - the per-thread handles, whose dirty stack the engine's make writes (search/06_nndirty.inc, nn_dirty.h);
//   - the five steps from the two network outputs to the static evaluation (nn_scale, nn_finalize);
//   - the UCI options of the evaluation constants and the diagnostic entry points (eval, evalbatch, nnueverify).
// Until 18/10/2026 this file was nnue_bridge.cpp.

#include "frozen.h"   // 🔴 DEVE stare qui: senza, il congelamento della
                      // miscela non si attiva nelle build di spedizione.
#include "consilium_bridge.h"

#include "profile.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#if defined(_MSC_VER)
    #include <intrin.h>   // _byteswap_uint64, _BitScanForward64
#endif

#ifdef TRIUMV_EMBED_RESOURCE
// Windows: the default network sits in an RCDATA resource of the exe (incbin does not work with _MSC_VER, clang-cl
// included). The pointers are resolved at the first load and read by load_net_impl below.
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
const unsigned char* gEmbeddedNNUEData = nullptr;
unsigned int         gEmbeddedNNUESize = 0;
static void embed_init_from_resource() {
    static bool done = false;
    if (done) return;
    done = true;
    // MAKEINTRESOURCEA(10) = RT_RCDATA in versione ANSI (RT_RCDATA segue UNICODE)
    HRSRC r = FindResourceA(nullptr, "NNUE_DEFAULT", MAKEINTRESOURCEA(10));
    if (!r) return;
    HGLOBAL h = LoadResource(nullptr, r);
    if (!h) return;
    gEmbeddedNNUEData = static_cast<const unsigned char*>(LockResource(h));
    gEmbeddedNNUESize = static_cast<unsigned int>(SizeofResource(nullptr, r));
}
#endif

#include "consilium/cn.h"

#include "large_pages.h"   // the network and the per-thread handles live on large pages when possible

// The few types and values the rest of the bridge uses, in a namespace of their own.
namespace cnb {

using Value    = int;
using Bitboard = std::uint64_t;
enum Color { WHITE, BLACK };

inline int pop_lsb(Bitboard& b) {
    const int s = __builtin_ctzll(b);
    b &= b - 1;
    return s;
}

// The engine's board as nn_scale reads it (pieces, counts per type, side to move).
class NnBoard {
   public:
    NnBoard(const unsigned long long* bb, const unsigned long long* occ, const int* mb, Color stm) :
        bb_(bb), occ_(occ), mb_(mb), stm_(stm) {}
    int   count() const { return __builtin_popcountll(occ_[2]); }
    int   count_type(int t) const { return __builtin_popcountll(bb_[t] | bb_[t + NN_BLACK]); }
    Color side_to_move() const { return stm_; }
    cn::Board view() const { return cn::Board{bb_, occ_, mb_, stm_ == BLACK}; }

   private:
    const unsigned long long* bb_;
    const unsigned long long* occ_;
    const int*                mb_;
    Color                     stm_;
};

}  // namespace cnb

#define EvalFileDefaultName "nn-consilium.nnue"

// GCC and clang builds from the Makefile (Linux, Android, MinGW): the default network is embedded with incbin when it
// sits at the build root (the Makefile sets NNUE_EMBEDDING_OFF otherwise). incbin.h is Dale Weiler's public-domain
// header (incbin/UNLICENCE). It defines gEmbeddedNNUEData and gEmbeddedNNUESize.
#if !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF) && !defined(UNIVERSAL_BINARY) && !defined(TRIUMV_EMBED_RESOURCE)
    #define INCBIN_SILENCE_BITCODE_WARNING
    #include "incbin/incbin.h"
INCBIN(EmbeddedNNUE, "nn-consilium.nnue");
    #define TRIUMV_EMBED_INCBIN
#endif

#if defined(_WIN32) && !defined(TRIUMV_NO_SHARED_NET) && !defined(TRIUMV_SHARED_NET)
    #define TRIUMV_SHARED_NET
#endif
#ifdef TRIUMV_SHARED_NET
    #include "shared_net.h"
#endif

using namespace cnb;

// The network: one block of fixed size on large pages, or the copy shared between processes (shared_net.h).
static cn::Net*        g_cn      = nullptr;
static const cn::Net*  g_cn_view = nullptr;
static std::string     g_net_mem_status;
static std::atomic<int> g_net_gen{1};
#define NET_REF (*g_cn_view)

const char* nn_net_memory_status(void) { return g_net_mem_status.c_str(); }

static int load_net_impl(const char* path) {
    if (!path || !*path)
        return 0;
#ifdef TRIUMV_EMBED_RESOURCE
    embed_init_from_resource();
#endif
    cn::Net* net = static_cast<cn::Net*>(large_pages_alloc(sizeof(cn::Net)));
    if (!net)
        return 0;
    std::string err;
    bool        ok;
    {
        std::ifstream f(path, std::ios::binary);
        if (f.good())
            ok = cn::load_file(path, net, &err);
        else {
            // No file: the embedded network, but only under the default name.
            std::string p(path);
            const size_t sl = p.find_last_of("/\\");
            const std::string base = sl == std::string::npos ? p : p.substr(sl + 1);
#if defined(TRIUMV_EMBED_RESOURCE) || defined(TRIUMV_EMBED_INCBIN)
            ok = base == EvalFileDefaultName && gEmbeddedNNUEData
              && cn::load_memory(gEmbeddedNNUEData, gEmbeddedNNUESize, net, &err);
#else
            ok = false;
            err = "file not found";
#endif
        }
    }
    extern bool g_startup_quiet;
    if (!ok) {
        large_pages_free(net);
        std::printf("info string ERROR: network %s not loaded (%s)\n", path, err.c_str());
        std::fflush(stdout);
        return 0;
    }
    if (!g_startup_quiet) {
        std::printf("info string NNUE evaluation using %s (Consilium, module cn)\n", path);
        std::fflush(stdout);
    }
    cn::Net* old = g_cn;
    g_cn      = net;
    g_cn_view = net;
#ifdef TRIUMV_SHARED_NET
    {
        std::string st;
        const void* sh = TriumvShm::attach(g_cn, sizeof(cn::Net), g_cn->contentHash ^ std::uint64_t(sizeof(cn::Net)), st);
        if (sh) {
            g_cn_view = static_cast<const cn::Net*>(sh);
            large_pages_free(g_cn);
            g_cn = nullptr;
        }
        g_net_mem_status = st;
        if (!g_startup_quiet) {
            std::printf("info string NNUE weights: %s\n", st.c_str());
            std::fflush(stdout);
        }
    }
#endif
    if (old)
        large_pages_free(old);
    ++g_net_gen;
    return 1;
}

// =====================================================================================================================
// From the two network outputs to the static evaluation (block 6 of 8.1, 18/10/2026)
//
// The network gives two values from the side to move's point of view: psqt (the PSQT sums of the accumulators) and
// positional (the layer stack). The evaluation the search sees is built from them in five steps:
//   1. blend   : a weighted sum of the two heads (positional weight of its own in endgames);
//   2. agree   : damped when the two heads disagree, since a large |psqt - positional| marks a position the network
//                reads in two different ways;
//   3. material: stretched with the material left on the board, and the search's optimism added on the same scale;
//   4. fifty   : pulled towards zero as the fifty-move counter grows;
//   5. scale   : the per-bucket EvalScale that matches the search margins, then the bounds of a normal score.
// Steps 1-3 do not depend on the fifty-move counter: they are the "unadjusted" value the evaluation cache stores, and
// nn_finalize applies steps 4-5 to it with the counter of the position that reads it.
//
// Step 2 is the linear damping v - v*d/EvalComplexDiv; the constants of steps 1-3 were tuned by our SPSA runs on this
// network (BAKE 09/09/2026 and BAKE MOE1 30/09/2026).
// Step 4 mixes two forms (18/10/2026), EvalFiftyMix /1024: the linear pull v - v*r/199 (0, the default) and a form of
// our own that keeps the full value for the first EvalFiftyKeep plies of reversible play, then falls linearly to 0 at
// ply 100, where a draw can be claimed (1024). On 2 million positions from games (tools/evalshape_predict.py) the
// second form predicts the result better in every band of the counter, and the SPSA run T15b raised the mix.
// (A second form of step 2, v*A/(A + d), predicted no better and the same SPSA run drove its mix to zero: removed on
// 18/10/2026.)
// =====================================================================================================================

// Per-bucket EvalScale (percent). The eight output buckets of the network (by piece count) each get their own scale;
// the UCI option EvalScale writes all eight, EvalScaleB0..B7 one each (BAKE MOE1, 30/09/2026).
static int g_eval_scale_pct  = 60;
static int g_eval_scale_b[8] = {60, 54, 60, 57, 59, 58, 59, 58};

// Terms of the last evaluation of one thread (kept in the thread's handle, not thread_local: the MinGW TLS emulation
// cost ~2.4% of the engine on 25/09/2026). unadjusted = steps 1-3 for the evaluation cache (EvalTTWrite); opt_base and
// opt_coeff split it as base + optimism * coeff / 1000 with EvalScale already inside coeff (EvalCacheOptSplit), so a
// cached entry can be finished with the optimism of the current iteration.
struct NnLast {
    int unadjusted = 0;
    int opt_base   = 0;
    int opt_coeff  = 0;
};

// Optimism: the search's bias towards its own side, written per side at the root of each iteration (by every
// thread for itself with OptPerThread, always on since the search rewrite of 04/10/2026). g_eval_optimism = 0 drops it.
int              g_eval_optimism  = 1;
std::atomic<int> g_optimism[2]    = {0, 0};   // relaxed: written by the main thread, read by the evaluations
int              g_opt_per_thread = 1;

// Constants of steps 1-3 (UCI options, table g_eval_consts below). All tuned by our SPSA runs; the old values are in
// parentheses. EvalPsqtW is left untuned on purpose: with EvalPosW it spans the "scale everything" direction that
// EvalScale already covers, so only their ratio moves.
int g_ev_psqt_w    = 125;     // psqt weight (/128)
int g_ev_pos_w     = 131;     // positional weight (/128)                       (126, BAKE MOE1)
int g_ev_pos_w_end = 131;     // positional weight with <= 15 pieces (/128); 125 measured -6.6 +- 8.3 (01/10/2026)
int g_ev_cplx_div  = 18198;   // disagreement that cancels the value             (19139, BAKE MOE1)
int g_ev_pawn_mat  = 680;     // pawn in the material term                       (551)
int g_ev_mat_base  = 102051;  // material scale: v * (base + material) / base    (84768)
int g_ev_opt_cplx  = 432;     // optimism grows by optimism * d / this           (461)
int g_ev_opt_base  = 5831;    // optimism scale: optimism * (this + material)    (6456)
// Step 4: the mix of the two forms (/1024, 0 = linear pull, identical tree) and the plies kept at full value.
int g_ev_fifty_mix  = 0;
int g_ev_fifty_keep = 20;

// Values of the minor and major pieces in the material term, and the bounds of a normal (non-mate, non-TB) score.
constexpr int MAT_KNIGHT = 781, MAT_BISHOP = 825, MAT_ROOK = 1276, MAT_QUEEN = 2538;
// Pawn weight and material base of the optimism term: the default values of EvalPawnMat and EvalMatBase, fixed.
constexpr int OPT_PAWN_MAT = 680, OPT_MAT_BASE = 102051;
constexpr int EVAL_BOUND = 31506;   // |static eval| <= 31506: below every tablebase and mate score

// Release builds (TRIUMV_FROZEN) compile the tuned values as literals: the divisions of steps 1-3 then become
// multiply-and-shift with identical integer results (as variables they were hardware divisions, one of them 64-bit:
// 57 cycles per node on 09/09/2026). The variables still exist for the UCI table; nn_frozen_check reports a change
// that such a build ignores.
#ifdef TRIUMV_FROZEN
#define g_ev_psqt_w 125
#define g_ev_pos_w 131
#define g_ev_pos_w_end 131
#define g_ev_cplx_div 18198
#define g_ev_pawn_mat 680
#define g_ev_mat_base 102051
#define g_ev_opt_cplx 432
#define g_ev_opt_base 5831
#endif

// Step 4 (fifty-move counter), shared by nn_scale and nn_finalize.
static inline int fifty_pull(int v, int rule50) {
    const int mix   = g_ev_fifty_mix;
    const int lin_v = v - v * rule50 / 199;
    if (mix == 0) return lin_v;
    const int r = std::min(rule50, 100), r0 = g_ev_fifty_keep;
    const int keep_v = r <= r0 ? v : int(std::int64_t(v) * (100 - r) / (100 - r0));
    return mix == 1024 ? keep_v : lin_v + int(std::int64_t(keep_v - lin_v) * mix / 1024);
}

// Step 5: per-bucket EvalScale and the bounds of a normal score.
static inline int scale_and_bound(int v, int scale_pct) {
    if (scale_pct != 100) v = int(std::int64_t(v) * scale_pct / 100);
    return std::clamp(v, -EVAL_BOUND, EVAL_BOUND);
}

static inline int nn_scale(const NnBoard& pos, Value psqt, Value positional, int rule50,
                           NnLast* last = nullptr, const int* opt_local = nullptr) {
    const int pieces = pos.count();

    // 1. blend
    const int wPos  = pieces <= 15 ? g_ev_pos_w_end : g_ev_pos_w;
    int       blend = (g_ev_psqt_w * int(psqt) + wPos * int(positional)) / 128;

    // 2. agree
    const int disagree = std::abs(int(psqt) - int(positional));
    blend -= blend * disagree / g_ev_cplx_div;

    // 3. material (and optimism on the same scale)
    const int material = g_ev_pawn_mat * pos.count_type(NN_PAWN) + MAT_KNIGHT * pos.count_type(NN_KNIGHT)
                       + MAT_BISHOP * pos.count_type(NN_BISHOP) + MAT_ROOK * pos.count_type(NN_ROOK)
                       + MAT_QUEEN * pos.count_type(NN_QUEEN);
    // With a negative pawn weight the factor could reach zero or below and flip the sign of the evaluation: it never
    // goes under a quarter of the base (at the defaults it is always above the base, so nothing changes).
    const std::int64_t stretched = std::int64_t(blend) * std::max(g_ev_mat_base + material, g_ev_mat_base / 4);
    // The optimism has its own material sum and scale (18/10/2026): with the pawn weight and the material base it
    // shared with the evaluation, a fit of those two (tools/evalscale_fit.py: negative pawn, base x27) would have
    // flipped the sign of the optimism or made it vanish. At the defaults (pawn 680, base 102051) the result is
    // identical bit for bit to the shared form.
    const int          opt_material = material + (OPT_PAWN_MAT - g_ev_pawn_mat) * pos.count_type(NN_PAWN);
    int                v;
    if (g_eval_optimism) {
        int opt = opt_local ? opt_local[pos.side_to_move()] : g_optimism[pos.side_to_move()].load();
        opt += opt * disagree / g_ev_opt_cplx;
        v = int((stretched + std::int64_t(opt) * (g_ev_opt_base + opt_material) * g_ev_mat_base / OPT_MAT_BASE)
                / g_ev_mat_base);
    } else
        v = int(stretched / g_ev_mat_base);

    const int scale_pct = g_eval_scale_b[(pieces - 1) / 4];   // the network's output bucket
    if (last) {
        last->opt_base   = int(stretched / g_ev_mat_base);
        last->opt_coeff  = int(std::int64_t(g_ev_opt_cplx + disagree) * (g_ev_opt_base + opt_material) * 1000 * scale_pct
                               / (std::int64_t(g_ev_opt_cplx) * OPT_MAT_BASE * 100));
        last->unadjusted = v;
    }

    // 4-5. fifty, scale and bounds
    return scale_and_bound(fifty_pull(v, rule50), scale_pct);
}

// ---------------------------------------------------------------------------
// Net loading entry points and options
// ---------------------------------------------------------------------------
const char* nn_default_net_name(void) { return EvalFileDefaultName; }

int nn_load_net(const char* net_path) { return load_net_impl(net_path); }
int nn_reload_big(const char* net_path) { return load_net_impl(net_path); }

// (nn_export_graft, nn_export_pst, nn_pst_indices e nn_graft_entries, per i blocchi da innesto: tolte il 10/10/2026,
// codice in _backup/Triumviratus_8.0_pre_rimozione_graft_2026-10-10 e nel repo del training, 04_consilium/graft_engine_storico.)

void nn_init_tables(void) {
    cn::init();   // feature index tables of the Consilium module (the slider tables are built by init_bitboards)
}

// No-ops kept for API stability (the module always uses its refresh cache).
void nn_set_finny(int) {}

void nn_acc_stats(void) {}

// M3 toggles. Incremental is now the DEFAULT: validated bit-exact vs full-refresh
// (zero verify mismatches + IDENTICAL bench node counts at depth 12/14 across all 8
// positions, exercising captures/promotions/e.p./castling) at +16% NPS. Toggle OFF
// ("incremental off") to fall back to the M2 full-refresh A/B base.
static bool g_incremental = true;
static bool g_verify      = false;
static unsigned long long g_verify_count = 0, g_verify_bad = 0;   // valutazioni confrontate / diverse

// NB (2026-07-15): "PsqtFastPath" (eval = solo psqt del net ai nodi |psqt|>soglia,
// saltando pairwise+propagate) PROVATO e UCCISO CON MISURA su build PGO:
// thr700 fire 75% -> albero +154%; thr2000 fire 16% -> +28%; thr4000 fire 0.6%
// -> +4%; time-to-depth SEMPRE peggiore. Terza falsificazione della famiglia
// "eval economica ai nodi decisi" (seconda rete piccola -17 Elo, SPLE): la search e'
// co-adattata all'eval piena, ogni surrogato grossolano gonfia l'albero piu'
// di quanto il forward risparmiato ripaghi. NON riprovare varianti.
void        nn_set_incremental(int on) { g_incremental = on != 0; }
void        nn_set_verify(int on) { g_verify = on != 0; }
void nn_set_eval_scale(int pct) {   // EvalScale: all eight buckets
    g_eval_scale_pct = pct < 1 ? 1 : pct;
    for (int b = 0; b < 8; ++b) g_eval_scale_b[b] = g_eval_scale_pct;
}
void nn_set_eval_scale_bucket(int b, int pct) {   // EvalScaleB0..B7
    if (b >= 0 && b < 8) g_eval_scale_b[b] = pct < 1 ? 1 : pct;
}
int nn_get_eval_scale_bucket(int b) { return (b >= 0 && b < 8) ? g_eval_scale_b[b] : 60; }

// The UCI options of the evaluation constants, in one table: uci_mt declares and routes everything listed here.
// The lower bounds of the divisors are not cosmetic: a zero would be a division by zero inside the evaluation.
// EvalBucketOverride is a probe (-1 = normal, 0..7 forces the output bucket), not a playing option.
#define CN_BUCKET_OVERRIDE (&cn::g_bucket_override)

#ifdef TRIUMV_FROZEN   // the table needs the variables, not the literals
#undef g_ev_psqt_w
#undef g_ev_pos_w
#undef g_ev_pos_w_end
#undef g_ev_cplx_div
#undef g_ev_pawn_mat
#undef g_ev_mat_base
#undef g_ev_opt_cplx
#undef g_ev_opt_base
#endif
namespace {
struct EvalConst { const char* name; int* var; int lo; int hi; };
const EvalConst g_eval_consts[] = {
    {"EvalPsqtW",          &g_ev_psqt_w,      40,    260},
    {"EvalPosW",           &g_ev_pos_w,       40,    260},
    {"EvalPosWEnd",        &g_ev_pos_w_end,   40,    260},
    {"EvalComplexDiv",     &g_ev_cplx_div,    4000,  60000},
    {"EvalPawnMat",        &g_ev_pawn_mat,    -30000, 1200},     // negative: more pawns, smaller evaluation
    {"EvalMatBase",        &g_ev_mat_base,    20000,  5000000},  // large: almost no stretch with the material
    {"EvalOptComplexDiv",  &g_ev_opt_cplx,    100,   2000},
    {"EvalOptMatBase",     &g_ev_opt_base,    1000,  30000},
    {"EvalBucketOverride", CN_BUCKET_OVERRIDE, -1,   7},
    {"EvalFiftyMix",       &g_ev_fifty_mix,   0,     1024},
    {"EvalFiftyKeep",      &g_ev_fifty_keep,  0,     90},
};
}
#ifdef TRIUMV_FROZEN
// Live values against the compiled literals, once per search: a release build ignores a change of these options and
// says so. It sits where the variables are visible (between the #undef above and the #define below).
void nn_frozen_check() {
    struct FzRef { const int* p; int val; const char* name; };
    static const FzRef fz[] = {
        {&g_ev_psqt_w, 125, "EvalPsqtW"},          {&g_ev_pos_w, 131, "EvalPosW"},
        {&g_ev_pos_w_end, 131, "EvalPosWEnd"},     {&g_ev_cplx_div, 18198, "EvalComplexDiv"},
        {&g_ev_pawn_mat, 680, "EvalPawnMat"},      {&g_ev_mat_base, 102051, "EvalMatBase"},
        {&g_ev_opt_cplx, 432, "EvalOptComplexDiv"}, {&g_ev_opt_base, 5831, "EvalOptMatBase"},
    };
    static bool reported[sizeof(fz) / sizeof(fz[0])] = {false};
    for (std::size_t j = 0; j < sizeof(fz) / sizeof(fz[0]); j++) {
        if (reported[j] || *fz[j].p == fz[j].val) continue;
        reported[j] = true;
        printf("info string WARNING: %s is compiled as %d in this build; the value %d is ignored\n", fz[j].name,
               fz[j].val, *fz[j].p);
        fflush(stdout);
    }
}
#endif
#ifdef TRIUMV_FROZEN
#define g_ev_psqt_w 125
#define g_ev_pos_w 131
#define g_ev_pos_w_end 131
#define g_ev_cplx_div 18198
#define g_ev_pawn_mat 680
#define g_ev_mat_base 102051
#define g_ev_opt_cplx 432
#define g_ev_opt_base 5831
#endif
int         nn_eval_const_count(void) { return int(sizeof(g_eval_consts) / sizeof(g_eval_consts[0])); }
const char* nn_eval_const_name(int i) { return g_eval_consts[i].name; }
int         nn_eval_const_get(int i) { return *g_eval_consts[i].var; }
int         nn_eval_const_lo(int i) { return g_eval_consts[i].lo; }
int         nn_eval_const_hi(int i) { return g_eval_consts[i].hi; }
int         nn_set_eval_const(const char* name, int value) {
    for (const EvalConst& c : g_eval_consts)
        if (!std::strcmp(c.name, name)) {
            *c.var = value < c.lo ? c.lo : (value > c.hi ? c.hi : value);
            return 1;
        }
    return 0;
}
int nn_get_eval_scale(void) { return g_eval_scale_pct; }   // to print "score cp" without EvalScale

// Steps 4-5 on an unadjusted value from the evaluation cache, with the fifty-move counter of the position that reads
// it: the same operations as the end of nn_scale, so a cached evaluation equals a fresh one for any counter. `bucket`
// is the output bucket of that position, (pieces - 1) / 4.
int nn_finalize(int unadjusted, int rule50, int bucket) {
    return scale_and_bound(fifty_pull(unadjusted, rule50), g_eval_scale_b[bucket < 0 ? 0 : (bucket > 7 ? 7 : bucket)]);
}

// ---------------------------------------------------------------------------
// Per-thread handle
// ---------------------------------------------------------------------------
namespace {

// Per-thread state: the module's handle (dirty stack + accumulators) on large pages, the optimism of the thread.
struct CnPos {
    cn::Handle* h;
    int         opt[2] = {0, 0};
    NnLast      last;
    CnPos() {
        h = static_cast<cn::Handle*>(large_pages_alloc(sizeof(cn::Handle)));
        std::memset(static_cast<void*>(h), 0, sizeof(cn::Handle));
        cn::reset(*h);
    }
    ~CnPos() { large_pages_free(h); }
};

// Accumulators for evaluations from scratch (incremental off, nnueverify, "eval"): they do not touch the chain of
// the search.
struct CnScratch {
    alignas(64) cn::Acc acc[2];
};
CnScratch& scratch() {
    thread_local CnScratch s;
    return s;
}

inline int eval_full(const NnBoard& board, int rule50, int* raw_out = nullptr, const int* opt_local = nullptr,
                     int* psqt_out = nullptr, int* pos_out = nullptr) {
    const cn::Out o = cn::evaluate_refresh(NET_REF, board.view(), scratch().acc);
    if (raw_out) *raw_out = o.psqt + o.positional;
    if (psqt_out) *psqt_out = o.psqt;
    if (pos_out) *pos_out = o.positional;
    return nn_scale(board, o.psqt, o.positional, rule50, nullptr, opt_local);
}

}  // namespace

void* nn_pos_create(void) { return new CnPos(); }
void  nn_pos_destroy(void* handle) { delete static_cast<CnPos*>(handle); }

int nn_last_unadjusted(void* handle) { return static_cast<CnPos*>(handle)->last.unadjusted; }
int nn_last_opt_base(void* handle) { return static_cast<CnPos*>(handle)->last.opt_base; }
int nn_last_opt_coeff(void* handle) { return static_cast<CnPos*>(handle)->last.opt_coeff; }

void nn_pos_set_optimism(void* handle, int w, int b) {
    CnPos* p  = static_cast<CnPos*>(handle);
    p->opt[0] = w;
    p->opt[1] = b;
}

void nn_pos_set(void* handle) { cn::reset(*static_cast<CnPos*>(handle)->h); }

NnStack* nn_pos_stack(void* handle) { return &static_cast<CnPos*>(handle)->h->stack; }

int nn_pos_eval(void* handle, const unsigned long long* bb, const unsigned long long* occ, const int* mailbox,
                int side_white, int rule50) {
    CnPos*        p = static_cast<CnPos*>(handle);
    const NnBoard board(bb, occ, mailbox, side_white ? WHITE : BLACK);
    const int*    opt = g_opt_per_thread ? p->opt : nullptr;
    if (!g_incremental)
        return eval_full(board, rule50, nullptr, opt);

    const cn::Out o   = cn::evaluate(NET_REF, *p->h, board.view());
    const int     inc = nn_scale(board, o.psqt, o.positional, rule50, nullptr, opt);

    if (g_verify) {
        // Same position from scratch: final values and both accumulators must be identical.
        const int  full    = eval_full(board, rule50, nullptr, opt);
        const bool accSame = cn::same_accumulators(*p->h, scratch().acc);
        if (inc != full || !accSame) {
            static int reported = 0;
            g_verify_bad++;
            if (reported++ < 64)
                std::printf("info string NNUE MISMATCH stati=%d inc=%d full=%d acc=%s\n", p->h->stack.size, inc, full,
                            accSame ? "uguale" : "DIVERSO");
            std::fflush(stdout);
        }
        g_verify_count++;
    }
    return inc;
}

unsigned long long nn_verify_count(void) { return g_verify_count; }
unsigned long long nn_verify_bad(void) { return g_verify_bad; }

// "eval" command and the golden-reference batch: from scratch on the engine's global board (mailbox rebuilt here).
static void mailbox_of(const unsigned long long* bb, int* mb) {
    for (int s = 0; s < 64; ++s) mb[s] = -1;
    for (int pc = 0; pc < 12; ++pc)
        for (Bitboard b = bb[pc]; b;) mb[pop_lsb(b)] = pc;
}

int nn_eval(int side_white, const unsigned long long* bb, const unsigned long long* occ, int rule50, int* raw_out) {
    int mb[64];
    mailbox_of(bb, mb);
    const NnBoard board(bb, occ, mb, side_white ? WHITE : BLACK);
    return eval_full(board, rule50, raw_out);
}

int nn_eval_parts(int side_white, const unsigned long long* bb, const unsigned long long* occ, int rule50,
                  int* psqt_out, int* pos_out) {
    int mb[64];
    mailbox_of(bb, mb);
    const NnBoard board(bb, occ, mb, side_white ? WHITE : BLACK);
    return eval_full(board, rule50, nullptr, nullptr, psqt_out, pos_out);
}
