// search/01_params.inc -- Parametri della ricerca e globali condivise con le altre unita'.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp.
//
// Riscrittura del 04/10/2026: i valori di partenza sono quelli della ricerca di Stockfish 19, espressi nelle sue
// unita' di valutazione. La nostra valutazione viene portata a quelle unita' da EvalUnits (09_history.inc), cosi'
// i margini restano confrontabili con l'originale. Ogni parametro e' un'opzione UCI nelle build di sviluppo e una
// costante nella build di release (TRIUMV_RELEASE), dove il compilatore la piega nel codice.

// ---------------------------------------------------------------------------
// Tabella dei parametri: nome, default, minimo, massimo.
// ---------------------------------------------------------------------------
#define TRIUMV_SEARCH_PARAMS(P)                                                                            \
  /* valutazione */                                                                                       \
  P(EvalUnits, 1355, 900, 1800)          /* /1024: unita' nostre -> unita' dei margini (pedone ~ 356) */  \
  P(OptStrength, 182, 0, 600)            /* optimism = forza * s / (|s| + div), in unita' della rete */   \
  P(OptDiv, 288, 1, 600)                                                                                  \
  /* finestre di aspirazione */                                                                           \
  P(AspBase, 5, 1, 40)                                                                                    \
  P(AspMeanSqDiv, 10193, 1000, 40000)                                                                     \
  P(AspGrow, 47, 10, 128)                /* /128 */                                                       \
  /* history: bonus e malus */                                                                            \
  P(BonusMul, 133, 50, 300)                                                                               \
  P(BonusSub, 81, 0, 300)                                                                                 \
  P(BonusMax, 1487, 500, 3000)                                                                            \
  P(BonusTTMove, 364, 0, 1000)                                                                            \
  P(BonusPrevStatDiv, 28, 4, 200)                                                                         \
  P(MalusMul, 968, 300, 2000)                                                                             \
  P(MalusSub, 235, 0, 800)                                                                                \
  P(MalusMax, 2244, 800, 5000)                                                                            \
  P(QuietBonusScale, 899, 400, 1600)     /* /1024 */                                                      \
  P(QuietMalusScale, 1159, 400, 2000)                                                                     \
  P(QuietMalusDecay, 921, 600, 1024)                                                                      \
  P(CaptBonusScale, 1427, 500, 2500)                                                                      \
  P(CaptMalusScale, 1489, 500, 2500)                                                                      \
  P(PrevContMalus, 713, 0, 1500)                                                                          \
  P(LowPlyScale, 712, 0, 1500)                                                                            \
  P(ContUpdScale, 750, 300, 1500)                                                                         \
  P(PawnBonusScale, 1104, 300, 2000)                                                                      \
  P(PawnMalusScale, 459, 100, 1500)                                                                       \
  P(EvalDiffMul, 11, 0, 40)               /* history della mossa precedente dalla differenza di eval */   \
  P(EvalDiffPawnMul, 13, 0, 40)                                                                           \
  /* ordinamento */                                                                                       \
  P(GoodQuietLimit, -14000, -30000, 0)                                                                    \
  P(QuietSortDepthMul, 3560, 1000, 8000)                                                                  \
  P(CheckOrderBonus, 16384, 0, 40000)                                                                     \
  P(ThreatOrderMul, 20, 0, 60)                                                                            \
  P(GoodCaptSeeDiv, 18, 4, 64)                                                                            \
  /* potatura prima del ciclo mosse */                                                                    \
  P(RazorMul, 482, 100, 1200)                                                                             \
  P(RfpMaxDepth, 19, 4, 30)                                                                               \
  P(RfpMultBase, 45, 10, 120)                                                                             \
  P(RfpMultDepth, 4, 0, 16)                                                                               \
  P(RfpMultCap, 85, 30, 200)                                                                              \
  P(RfpNoTTHit, 20, 0, 60)                                                                                \
  P(RfpImproving, 2789, 0, 5000)                                                                          \
  P(RfpOppWorse, 335, 0, 1500)                                                                            \
  P(RfpCorrDiv, 198435, 50000, 800000)                                                                    \
  P(RfpBetaWeight, 661, 0, 1024)                                                                          \
  P(NmpDepthMul, 13, 0, 40)                                                                               \
  P(NmpImproving, 47, 0, 150)                                                                             \
  P(NmpBase, 365, 0, 800)                                                                                 \
  P(NmpRedBase, 7, 3, 12)                                                                                 \
  P(NmpEvalDiv, 256, 64, 1024)                                                                            \
  P(NmpVerifyDepth, 16, 6, 30)                                                                            \
  P(ProbCutMargin, 241, 80, 500)                                                                          \
  P(ProbCutImproving, 64, 0, 200)                                                                         \
  P(ProbCutTTMargin, 428, 150, 900)                                                                       \
  /* potatura nel ciclo mosse */                                                                          \
  P(CaptFutBase, 234, 50, 600)                                                                            \
  P(CaptFutDepth, 247, 80, 500)                                                                           \
  P(CaptFutHist, 134, 0, 400)            /* /1024 */                                                      \
  P(CaptSeeDepth, 177, 50, 400)                                                                           \
  P(CaptSeeHist, 34, 0, 120)             /* /1024 */                                                      \
  P(QuietHistPrune, 4136, 1000, 10000)                                                                    \
  P(QuietMainHistMul, 69, 0, 200)        /* /32 */                                                        \
  P(QuietFutDepth, 119, 40, 300)                                                                          \
  P(QuietFutAlpha, 90, 0, 300)                                                                            \
  P(QuietFutBase, 164, 0, 400)                                                                            \
  P(QuietSeeMul, 23, 5, 80)                                                                               \
  /* estensioni */                                                                                        \
  P(SingMarginBase, 59, 20, 150)                                                                          \
  P(SingMarginTtPv, 66, 0, 200)                                                                           \
  P(SingDoubleBase, -2, -100, 100)                                                                        \
  P(SingDoublePv, 204, 0, 500)                                                                            \
  P(SingDoubleQuiet, 152, 0, 400)                                                                         \
  P(SingTripleBase, 70, 0, 300)                                                                           \
  P(SingTriplePv, 279, 0, 600)                                                                            \
  P(SingTripleQuiet, 188, 0, 500)                                                                         \
  P(SingTripleTtPv, 81, 0, 300)                                                                           \
  /* riduzioni (1/1024 di ply) */                                                                         \
  P(LmrLogMul, 2872, 1500, 4000)         /* /128 */                                                       \
  P(LmrDeltaMul, 577, 0, 1500)                                                                            \
  P(LmrNotImproving, 197, 0, 600)        /* /512 */                                                       \
  P(LmrBase, 982, 0, 2500)                                                                                \
  P(LmrTtPvAdd, 929, 0, 2500)                                                                             \
  P(LmrTtPvSub, 3023, 0, 6000)                                                                            \
  P(LmrTtPvPv, 1004, 0, 3000)                                                                             \
  P(LmrTtPvAlpha, 885, 0, 3000)                                                                           \
  P(LmrTtPvDepth, 816, 0, 3000)                                                                           \
  P(LmrTtPvCut, 940, 0, 3000)                                                                             \
  P(LmrOffset, 697, -1500, 2500)                                                                          \
  P(LmrMoveCount, 65, 0, 300)                                                                             \
  P(LmrCorrDiv, 26310, 5000, 100000)                                                                      \
  P(LmrCutNode, 4026, 1000, 7000)                                                                         \
  P(LmrCutNoTT, 933, 0, 3000)                                                                             \
  P(LmrTTCapture, 1079, 0, 3000)                                                                          \
  P(LmrCutoffBase, 264, 0, 1500)                                                                          \
  P(LmrCutoffMany, 1095, 0, 3000)                                                                         \
  P(LmrCutoffAll, 1138, 0, 3000)                                                                          \
  P(LmrTTMove, 2179, 0, 5000)                                                                             \
  P(LmrStatDiv, 439, 100, 1500)          /* /4096 */                                                      \
  P(LmrAllNode, 276, 0, 800)                                                                              \
  P(LmrNoTTFull, 1127, 0, 3000)                                                                           \
  P(DeeperMargin, 53, 0, 200)                                                                             \
  P(ShallowerMargin, 8, 0, 60)                                                                            \
  /* quiescenza */                                                                                        \
  P(QsFutMargin, 306, 100, 600)                                                                           \
  P(QsSeeMargin, 74, 0, 300)                                                                              \
  /* correction history */                                                                                \
  P(CorrPawnW, 15341, 0, 40000)                                                                           \
  P(CorrMinorW, 10569, 0, 40000)                                                                          \
  P(CorrNonPawnW, 12906, 0, 40000)                                                                        \
  P(CorrContW, 8761, 0, 30000)                                                                            \
  P(CorrMinorUpd, 150, 0, 400)           /* /128 */                                                       \
  P(CorrNonPawnUpd, 186, 0, 400)                                                                          \
  /* gestione del tempo (in millesimi) */                                                                 \
  P(TmFallBase, 1148, 500, 2500)         /* /100: fallingEval = (base + a*(prev-best) + b*(iter-best)) */ \
  P(TmFallPrev, 230, 0, 600)                                                                              \
  P(TmFallIter, 110, 0, 400)                                                                              \
  P(TmInstabBase, 1077, 500, 2000)                                                                        \
  P(TmInstabMul, 2229, 0, 5000)

#ifdef TRIUMV_RELEASE
  #define TP_DECLARE(name, def, lo, hi) static constexpr int name = def;
#else
  #define TP_DECLARE(name, def, lo, hi) static int name = def;
#endif
TRIUMV_SEARCH_PARAMS(TP_DECLARE)
#undef TP_DECLARE

// ---------------------------------------------------------------------------
// Opzioni che non sono parametri numerici della ricerca.
// ---------------------------------------------------------------------------
static int  g_multipv            = 1;
static bool g_show_wdl           = false;
static bool g_ponder_option      = false;    // "Ponder": la GUI pondera -> +25% di tempo ottimale (come SF)
int         g_syzygy_probe_depth = 1;
int         g_syzygy_probe_limit = 7;
static bool g_syzygy_rule50      = true;
static bool g_eval_off           = false;    // diagnostica NPS: valutazione di solo materiale
static bool g_data_log_enabled   = false;
static std::string g_data_log_file = "triumviratus_dataset.txt";
void set_data_log_enabled(bool enabled) { g_data_log_enabled = enabled; }

// Globali lette da altre unita' di compilazione (tt.h, see.cpp, movegen.cpp). I nomi restano quelli di prima.
tt_entry* hash_table      = nullptr;
U64       hash_entries    = 0;
bool      g_tt_pow2       = false;
int       current_age     = 0;
bool      g_tt_twolevel   = true;
bool      g_ttmove24      = true;
int       g_tt_keep_margin = 3;   // si sovrascrive se la nuova non e' piu' corta di 4 ply (+2 sui nodi PV), come SF
bool      g_tt_move_keep  = true; // senza mossa nuova si conserva quella vecchia
bool      g_tt_age_refresh = false;
bool      g_large_pages   = true;
bool      g_see_fix       = true;
bool      g_ep_key_fix    = true;
static const bool g_evasion_gen = true;

// Stockfish piece codes indexed by Triumviratus piece (P,N,B,R,Q,K,p,n,b,r,q,k).
static const int nn_piece_code[12] = {1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14};

static inline bool parse_bool_value(int v) { return v != 0; }

void print_search_options() {
#ifndef TRIUMV_RELEASE
#define TP_PRINT(name, def, lo, hi) printf("option name " #name " type spin default %d min %d max %d\n", def, lo, hi);
  TRIUMV_SEARCH_PARAMS(TP_PRINT)
#undef TP_PRINT
#endif
}

bool set_search_param(const char* name, int value) {
  if (!strcmp(name, "MultiPV")) { g_multipv = value < 1 ? 1 : (value > 64 ? 64 : value); return true; }
  if (!strcmp(name, "UCI_ShowWDL")) { g_show_wdl = parse_bool_value(value); return true; }
  if (!strcmp(name, "Ponder")) { g_ponder_option = parse_bool_value(value); return true; }
  if (!strcmp(name, "SyzygyProbeDepth")) { g_syzygy_probe_depth = value < 1 ? 1 : value; return true; }
  if (!strcmp(name, "SyzygyProbeLimit")) { g_syzygy_probe_limit = value < 0 ? 0 : (value > 7 ? 7 : value); return true; }
  if (!strcmp(name, "Syzygy50MoveRule")) {
    g_syzygy_rule50 = parse_bool_value(value);
    syzygy_set_ignore_50_move_rule(!g_syzygy_rule50);
    return true;
  }
  if (!strcmp(name, "EvalOff")) { g_eval_off = parse_bool_value(value); return true; }
#ifndef TRIUMV_RELEASE
#define TP_SET(pname, def, lo, hi)                                                                         \
  if (!strcmp(name, #pname)) { pname = value < lo ? lo : (value > hi ? hi : value); return true; }
  TRIUMV_SEARCH_PARAMS(TP_SET)
#undef TP_SET
#endif
  return false;
}
