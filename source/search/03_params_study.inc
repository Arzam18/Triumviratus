// search/03_params_study.inc -- Switch di studio, set_search_param e setter UCI.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// Switch di studio 2026-09-07 (tutti default = comportamento storico, bench
// 279691 byte-identico). Uno per ipotesi, per SPRT separati. Vedi
// STUDY_PLAN_7.0_2026-09-07.md §2.
// ============================================================================
// TTCutDepthOverride (SF step 6 `|| depth > 4`): il cutoff TT incoerente col tipo
// di nodo (bound fail-high a un all-node o viceversa) viene comunque accettato
// sopra questa profondita'. 0 = mai (storico).
int g_ttcut_depth_override = 0;
// TTFailLowMove: 1 = storico, lo store di un nodo fail-low porta la "migliore
// delle fallite" come TT move. 0 = come SF: nessuna mossa sui bound UPPER
// (TTMoveKeep conserva quella gia' presente per la stessa posizione).
int g_tt_faillow_move = 1;
// TTPvInherit (SF: `if (bestValue <= alpha) ss->ttPv |= (ss-1)->ttPv`): un nodo
// fail-low eredita il flag ttPv del padre nello store.
int g_ttpv_inherit = 0;
// NMPCutNodeOnly (SF step 10 `cutNode &&`): null move solo ai cut-node.
int g_nmp_cutnode_only = 0;
// IIRNoAllNode (SF step 11 `!allNode`): niente IIR ai nodi ALL (ne' PV ne' cut).
// I quattro switch qui sotto (IIRNoAllNode, ProbCutTTAll, CorrFailLowAll e
// NMPVerifDepth) sono stati misurati insieme a 10+0.1 hash 64 su 11.207 partite:
// il lato con gli switch accesi fa 49,23% contro 50,77%, cioe' **perdono ~5 Elo**.
// Restano dichiarati per poterli riprovare singolarmente, spenti di default.
// 📌 Convenzione di misura: fastchess riporta Elo/Points/LOS del PRIMO motore
// elencato. `_rig_locale/run.sh` mette `test` per primo, quindi Elo positivo =
// la patch guadagna. Se si legge un run vecchio, controllare l'ordine nel README.
int g_iir_no_allnode = 0;
// ProbCutTTAll (SF step 13): il probcut da TT (bound LOWER, depth >= depth-4,
// score >= beta+margine) vale 1 = anche fuori scacco, 2 = anche con TT move
// non-cattura, 3 = anche ai nodi PV. 0 = solo in scacco (storico).
int g_probcut_tt_all = 0;
// RFPTTMoveGate (SF step 9 `(!ttData.move || ttCapture)`): niente RFP quando la
// TT move e' una quiet.
int g_rfp_ttmove_gate = 0;
// CorrFailLowAll (SF step 24): ai nodi fail-low la correction history si aggiorna
// SEMPRE (SF non ha best move su un fail-low, quindi il gate "best quiet" non
// esiste); 2 = anche col peso 18/12 di SF sui fail-low.
int g_corr_faillow_all = 0;
// AlphaDepthDecAmt (SF step 22 `depth -= 3` per 3 < depth < 12): quanto ridurre la
// depth delle mosse restanti quando una mossa alza alpha. 1 = storico.
// ⭐ BAKATO 08/09/2026 (run s10_alphadepthdec3): +8,64 ± 3,39 su 12.022 partite,
// LOS 100%, LLR 2,95 — l'unico dei test di studio ad aver ATTRAVERSATO la soglia
// 2,94 invece di essere fermato a mano. 10+0.1 hash 64.
// Stockfish (Step 22) scende di 3 ply quando una mossa alza alpha; noi scendevamo
// di 1. Il nodo ha gia' trovato un best "vero": le mosse restanti devono solo
// battere quello, e non meritano lo stesso sforzo. Sul bench toglie il 44,7%
// dell'albero, ed e' la terza leva di potatura della sessione — coerente con la
// misura del 07/09 (73,5% dei nostri nodi a depth 1-3 contro il 58,4% di SF).
// AlphaDepthDecAmt: 3 = la forma di SF (Step 22, depth -= 3 quando alpha si
// alza), 1 = la nostra. Misurato a 10+0.1 su 12.026 partite: il lato a 3 fa
// 48,75% contro 51,25%, quindi la forma di SF qui **perde ~8,6 Elo**.
int g_alpha_depth_dec_amt = 1;
int g_lmr_alpha_lo = 64;  // clamp inferiore del divario (alpha-eval), in cp
int g_lmr_alpha_hi = 96;  // clamp superiore
// ⭐ ContHist4LMR (2026-07-20, spin, 0 = OFF byte-identico) — SEGNALE ORFANO,
// non feature nuova. Il ramo LMR *legacy* (`if (!g_lmr_fine)`, :4878-4965, oggi
// irraggiungibile perche' LMRFine e' bakato ON) consultava la continuation
// history a 1, 2 **e 4** ply indietro (:4937). Il port di LMRFine da SF usa
// `2*main + cont1 + cont2` e la 4-ply e' rimasta fuori: non un bug, un port
// fedele — ma da noi e' una PERDITA, perche' `cont_hist_4` continuiamo a
// MANTENERLA a ogni nodo (update :3834) e a leggerla nell'ordinamento (:2852).
// Paghiamo il costo e la LMR la ignora. Coefficiente SEPARATO (non dentro
// sscore) per due motivi: 0 resta byte-identico, e SPSA puo' pesare la 4-ply
// senza dover muovere LMRFSS, che governa main+cont1+cont2.
int g_lmrf_cont4 =
    0; // peso della conthist a 4 ply nella LMR fine (1/4096, come LMRFSS)
int g_lmr_expect =
    0; // LMRExpect (spin, 0=OFF byte-identico): bonus EXTRA al termine
       // cutoffCnt quando il nodo e' ALL (ne' PV ne' cut) = "aspettativa
       // rispettata". Reckless usa +256/+384 nelle stesse unita' (1/1024 ply).
// EvalOptimism (5.1): alimenta g_optimism (nnue_bridge) dalla root col contempt
// dinamico di SF (optimism = strength*score/(|score|+div)). ⚠️ DEFAULT = ON
// (bakato 5.1; il valore vive in nnue_bridge.cpp:138, non qui). Spegnendolo
// g_optimism resta 0 = byte-identico al pre-5.1 — ma NON e' lo stato di
// default.
extern int g_eval_optimism; // definito in nnue_bridge.cpp
extern std::atomic<int>
    g_optimism[2]; // F-019: atomic (main scrive, helper leggono)
extern int g_opt_per_thread; // OptPerThread (SMP, 26/09/2026), definito in nnue_bridge.cpp
// 🔴 BAKE 09/09/2026: co-tunati insieme ai sei del blend eval (nnue_bridge.cpp:176),
// stesso vettore SPSA, stessa doppia conferma (+2,71 a 10+0.1, +3,75 a 20+0.2 hash 256).
// Salgono ENTRAMBI, il che lascia l'optimism quasi invariato sui punteggi grandi e ne
// cambia la forma su quelli piccoli.
int g_opt_strength =
    182; // [BAKE 170->182]  optimism = strength*score/(|score|+div)
int g_opt_div = 288; // [BAKE 272->288]
// ContHistPruneDepth + HistPruneMargin: la coppia (6, 1200) allarga la history
// pruning verso la forma di SF, che non ha cap di profondita'. Misurata a 10+0.1
// su 19.241 partite: 49,36% contro 50,64%, cioe' **perde ~4,5 Elo**. I due vanno
// mossi INSIEME — separati sono inerti: col cap a 2 il margine sposta lo 0,2% del
// bench, e col margine a 2097 il cap non cambia un byte, perche' a prune_depth
// 3-6 la soglia -2097*depth chiede una history piu' negativa di quanto le due
// tabelle sommate possano essere (|main| + |cont| <= 2*HISTORY_MAX = 14000).
int g_histprune_margin =
    2097; // era 2097 [3.7 BAKE 1602->1691; BAKED #1 era 1000]. history pruning:
          // prune late quiet if combined hist < -margin*depth
int g_conthist_prune_depth =
    2; // era 2. gate profondita' conthist-prune (SF usa lmrDepth<6). UCI
       // ContHistPruneDepth. PASSO2: col blocco LmrDepthPrune si alza ~6.
// LmrDepthPrune (SF): pota futility+conthist sulla profondita' RIDOTTA dalla
// LMR per la mossa (prune_depth = depth-1-lmr_table[depth][movecount]) invece
// che sulla depth piena del nodo. Fa scattare il pruning sulle mosse TARDIVE
// anche a node-depth alto (chiude il gap-midgame vs SF, che pota 3x di piu'
// li'). Cap nostri (6/4) = MENO aggressivo di SF (15/6). Spin LmrDepthPrune:
// 0=off (byte-identico), 1=on. Co-tunabile.
int g_lmrdepth_prune = 1;
// PASSO 1 (SF Step-14): protezione-history. Con LmrDepthPrune ON, prune_depth
// viene AGGIUSTATO dalla history della mossa: prune_depth +=
// history/g_lmrdepth_histdiv. History buona -> prune_depth sale (mossa
// protetta); scarsa -> scende (potata di piu'). E' il pezzo che mancava al port
// monco (potava le tardive alla cieca -> bloat). SF div=6437.
int g_lmrdepth_histdiv = 4509;
// SEE-pruning margins (ALSO the Phase-2 skip_bad_caps lever): a move is
// SEE-pruned at low depth if SEE < -g_see_cap_margin*depth (captures) or <
// -g_see_quiet_margin*depth*depth (quiets). Exposed so SPSA can tune them (UCI:
// SEECaptureMargin / SEEQuietMargin).
int g_see_cap_margin = 81; // REVERT 2026-07-23 (SPSA B1 evaporato @4452g)
int g_see_quiet_margin = 116; // [3.7 BAKE 98->96; BAKED #1 era 50]
int g_see_depth = 3; // gate profondita' del SEE pruning (alzare = potare piu'
                     // in profondita'). UCI SEEPruneDepth, tunabile.
// BadCapSkipAfter (default 1 = byte-identico): quante bad capture SEE-potate
// servono, IN QUESTO NODO, prima di spegnere l'intero stage MPS_BAD_TACTICAL.
// Il commento di quello skip dichiara che le bad capture escono in ordine di
// SEE decrescente: NON e' vero. td_score_move le ordina per SCORE_BAD_CAPTURE +
// mvv_lva + caphist e MPS_BAD_TACTICAL fa il max su quello score; mvv_lva +
// caphist non e' monotono nel SEE. Una DxT difesa (mvv alto, SEE ~-400) esce
// PRIMA di una CxP difesa (mvv basso, SEE ~-220): la prima fa scattare lo skip
// e si porta via la seconda, che il test avrebbe fatto passare. 0 = skip
// disattivato (ogni bad capture paga il proprio td_see_at_least); 1 = storico.
// Vale solo a depth <= g_see_depth. 2 = servono due prove prima di fidarsi.
// Max 2 e non 3: il blocco vive solo a depth <= g_see_depth (3) e incrementa
// solo se la bad capture fallisce SEE < -81*depth (a depth 3: -243). Tre
// fallimenti cosi' nello STESSO nodo praticamente non capitano -> 3 sarebbe un
// no-op equivalente a 0.
// ATTENZIONE al costo: a 0 lo stage non si spegne mai, quindi le bad capture che
// PASSANO il margine dopo un fallimento precedente non vengono solo ri-testate,
// vengono CERCATE: sono sottoalberi nuovi. E' il meccanismo che dovrebbe portare
// l'Elo ed e' anche il motivo per cui il segno puo' essere negativo -> node count
// a 13/17/20 obbligatorio prima dell'SPRT.
int g_badcap_skip_after = 1;
// S-05 (2026-07-06, terzo audit): default OFF. Oggi il gate/margine SEE usa la
// depth PIENA del nodo -> una mossa tardiva/ridotta-LMR a node-depth alto non
// viene MAI SEE-potata (depth<=3 non scatta mai oltre il ply 3). `prune_depth`
// (gia' calcolato sopra per futility/conthist, vedi LmrDepthPrune) e'
// esattamente la profondita'-effettiva ridotta per le mosse quiet tardive (per
// le catture prune_depth==depth, quindi ON questo toggle non le tocca) — usarlo
// al posto di depth chiude il gap di DOMINIO (non di costanti) segnalato da SF
// Step 14.
static bool g_see_lmr_depth = false;
// Dedicated margin for the SEELmrDepth quiet path: g_see_quiet_margin is shared
// with the always-on capped path (depth<=g_see_depth), so tuning it would also
// perturb that default behavior. This one only feeds the prune_depth-based
// quadratic when the toggle is ON -> SPSA can search it independently. Same
// starting value as g_see_quiet_margin (185) until tuned.
int g_see_lmr_quiet_margin = 193;
// Dedicated cap on prune_depth for the SEELmrDepth path (separate from
// SEEPruneDepth, which still gates the raw-depth/capture path). Without ANY cap
// we pay a real td_see_at_least() call for every quiet move at every
// prune_depth, even where the quadratic margin is so permissive it
// mathematically can never prune -> wasted cycles past some point. Default 32
// is practically unbounded (prune_depth rarely reaches it), matching the tested
// no-cap behavior; SPSA can search LOWER values to trade reach for NPS.
int g_see_lmr_prune_cap = 27;
// S-05 forma LINEARE (2026-07-12, default OFF): il margine quiet del path
// SEELmr e' oggi quadratico (-coef*prune_depth^2), come il path di default. Il
// vecchio SPSA S-05 si e' hard-stoppato a 178/300 con gradiente PIATTO:
// l'ipotesi e' che il quadrato saturi il margine cosi' in fretta da rendere il
// coefficiente quasi indistinguibile per SPSA (a prune_depth 3 gia'
// -185*9=-1665, praticamente mai raggiungibile -> il gradiente non vede il
// coef). La forma lineare -coef*prune_depth cresce piano -> il coef resta
// nell'intervallo utile a piu' profondita' -> gradiente piu' pulito, e potatura
// meno aggressiva alle prune_depth alte. Quando g_see_lmr_linear e' ON il path
// SEELmr usa -g_see_lmr_linear_coef*prune_depth al posto del quadrato. Attiva
// il path da solo (non serve anche SEELmrDepth). coef alto = soglia piu'
// negativa = pota MENO (albero piu' grande).
static bool g_see_lmr_linear = false;
int g_see_lmr_linear_coef = 368; // center; gate 2-3 valori lato laptop
// MultiPV (analisi, stile Stockfish, 2026-07-13): quante varianti di root
// riportare. 1 = gioco normale, flusso INVARIATO (gate: bench identico). >1:
// dopo la linea principale di ogni iterazione il main thread ri-cerca la root a
// finestra piena escludendo le best move gia' assegnate e stampa "info ...
// multipv k ...". Solo analisi: TM/voting/FollowPV/optimism restano ancorati
// alla linea 1; gli helper lazy-SMP restano single-PV (riempiono la TT come
// sempre).
static int g_multipv = 1;
// TTPrefetch (Reckless #1085): prefetch del bucket TT del FIGLIO in
// td_make_move, appena la chiave e' definitiva (post-legalita'). Node-identical
// per costruzione: solo NPS. Il tentativo 2026-06-07 era NPS-neutral;
// ri-misurato 2026-07-13 (dopo il bake di TTTwoLevel, che ha cambiato il
// pattern di accesso TT) su VM GCP: +1.88% NPS su N=100 bench (OFF 1061381
// [1011790-1106739] vs ON 1081343 [1048327-1113268], range quasi disgiunti).
// BAKED ON di default (option resta per un'eventuale ablazione A/B).
static bool g_tt_prefetch = true;
// GoodCapTTQuiet (Reckless 0.10.0 #1107, default OFF): se la TT-move del nodo
// e' QUIET, la mossa migliore nota non e' una cattura -> per essere ordinata
// "good" una cattura deve VINCERE materiale netto (SEE >= +1; le pari scendono
// tra le bad). Reckless: +1.48 +-1.98 @30k games -> micro-gain, candidato
// co-tune, da gatare ad alto N. (La loro condizione extra noisy_count>2 e' un
// artefatto del loro picker a stadi; qui si usa la sola condizione
// tt-move-quiet.)
static bool g_goodcap_ttquiet = false;
// UCI_ShowWDL (analisi): quando ON, le info-line riportano " wdl W D L"
// (permille, stm-relative, somma 1000) accanto allo score. Modello logistico a
// un parametro ancorato al punto di calibrazione (NORM_CP=449 intrinseco = +100cp
// normalizzato = P(win)~50%, rifatto il 10/09/2026 su 10,7M posizioni della 7.0;
// era 392, misurato sulla rete della 5.1).
// Larghezza b=100 -> patta ~46% a posizione pari (coerente col draw-ratio
// osservato ~49%). Default OFF = output storico invariato. Stima onesta, non un
// fit materiale-dipendente alla SF.
static bool g_show_wdl = false;
// (The SFNNv10 eval-wrapper tunables — g_small_net_threshold / g_eval_optimism
// / g_eval_pawn_scale / g_eval_complexity_div / g_eval_blend_delta — were
// removed in M2: the TRANN1 bridge applies Stockfish's fixed cp scaling, so
// they no longer exist. The matching UCI options + set_search_param cases were
// removed too.)

void init_lmr_table(); // defined below; re-run when LMR core coefficients
                       // change

// Dispatch a UCI spin option to the matching tunable. Returns true if matched.
bool set_search_param(const char *name, int value) {
  if (!strcmp(name, "RFPMargin")) {
    g_rfp_margin = value;
    return true;
  }
  if (!strcmp(name, "RazorBase")) {
    g_razor_base = value;
    return true;
  }
  if (!strcmp(name, "RazorMult")) {
    g_razor_mult = value;
    return true;
  }
  if (!strcmp(name, "FutilityBase")) {
    g_fut_base = value;
    return true;
  }
  if (!strcmp(name, "FutilityMult")) {
    g_fut_mult = value;
    return true;
  }
  if (!strcmp(name, "FutilityDepth")) {
    g_fut_depth = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "SEEPruneDepth")) {
    g_see_depth = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "SEELmrDepth")) {
    g_see_lmr_depth = value != 0;
    return true;
  }
  if (!strcmp(name, "SEELmrQuietMargin")) {
    g_see_lmr_quiet_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "SEELmrPruneCap")) {
    g_see_lmr_prune_cap = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "SEELmrLinear")) {
    g_see_lmr_linear = value != 0;
    return true;
  }
  if (!strcmp(name, "SEELmrLinearCoef")) {
    g_see_lmr_linear_coef = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "MultiPV")) {
    g_multipv = value < 1 ? 1 : (value > 64 ? 64 : value);
    return true;
  }
  if (!strcmp(name, "UCI_ShowWDL")) {
    g_show_wdl = value != 0;
    return true;
  }
  if (!strcmp(name, "TTPrefetch")) {
    g_tt_prefetch = value != 0;
    return true;
  }
  if (!strcmp(name, "GoodCapTTQuiet")) {
    g_goodcap_ttquiet = value != 0;
    return true;
  }
  if (!strcmp(name, "QsStalemateCheck")) {
    g_qs_stalemate_check = value != 0;
    return true;
  }
  if (!strcmp(name, "SyzygyProbeDepth")) {
    g_syzygy_probe_depth = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "SyzygyProbeLimit")) {
    g_syzygy_probe_limit = value < 0 ? 0 : (value > 7 ? 7 : value);
    return true;
  }
  if (!strcmp(name, "Syzygy50MoveRule")) {
    syzygy_set_ignore_50_move_rule(value == 0); // UCI true = rispetta (default) -> ignore=false
    return true;
  }
  if (!strcmp(name, "CorrUncert")) {
    g_corr_uncert = value != 0;
    return true;
  }
  if (!strcmp(name, "CorrUncertRFP")) {
    g_cu_rfp = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CorrUncertFut")) {
    g_cu_fut = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CorrUncertCap")) {
    g_cu_cap = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "BrilliantSac")) {
    g_brilliant_sac = value != 0;
    return true;
  }
  if (!strcmp(name, "BrilliantSacMargin")) {
    g_brilliant_sac_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "BrilliantSacLMR")) {
    g_brilliant_sac_lmr = value != 0;
    return true;
  }
  if (!strcmp(name, "BrilliantSacLMRAmt")) {
    g_brilliant_sac_lmr_amt = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TroubleMaking")) {
    g_trouble = value != 0;
    return true;
  }
  if (!strcmp(name, "TroubleScore")) {
    g_trouble_score = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TroubleEffort")) {
    g_trouble_effort = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TroubleMargin")) {
    g_trouble_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "FutilityImproving")) {
    g_fut_improving = value;
    return true;
  }
  if (!strcmp(name, "SingularDoubleMargin")) {
    g_singular_dmargin = value;
    return true;
  }
  if (!strcmp(name, "HistReductionDiv")) {
    g_hist_red_div = value;
    return true;
  }
  if (!strcmp(name, "AspInitDelta")) {
    g_asp_init_delta = value;
    return true;
  }
  if (!strcmp(name, "AspStableShrink")) {
    g_asp_stable_shrink = value != 0;
    return true;
  }
  if (!strcmp(name, "AspStableCap")) {
    g_asp_stable_cap = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "AspGrow")) {
    g_asp_grow = value;
    return true;
  }
  if (!strcmp(name, "AspScoreMult")) {
    g_asp_score_mult = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CMHCPly1")) {
    g_cmhc_ply1 = value != 0;
    return true;
  }
  if (!strcmp(name, "Rule50Formula")) {
    g_rule50_formula = value != 0;
    return true;
  }
  if (!strcmp(name, "CorrTBGuard")) {
    g_corr_tb_guard = value != 0;
    return true;
  }
  if (!strcmp(name, "PrevRefuteMalus")) {
    g_prev_refute_malus = value < 0 ? 0 : (value > 200 ? 200 : value);
    return true;
  }
  if (!strcmp(name, "ProbCutAdj")) {
    g_probcut_adj = value != 0;
    return true;
  }
  if (!strcmp(name, "RFPNoTTPv")) {
    g_rfp_no_ttpv = value != 0;
    return true;
  }
  if (!strcmp(name, "PruneNPMGate")) {
    g_prune_npm_gate = value != 0;
    return true;
  }
  if (!strcmp(name, "FutFailSoft")) {
    g_fut_fail_soft = value != 0;
    return true;
  }
  if (!strcmp(name, "FHBlend")) {
    g_fh_blend = value != 0;
    return true;
  }
  if (!strcmp(name, "ExclPruneGate")) {
    g_excl_prune_gate = value < 0 ? 0 : (value > 2 ? 2 : value);
    return true;
  }
  if (!strcmp(name, "NegExtOrder")) {
    g_negext_order = value != 0;
    return true;
  }
  if (!strcmp(name, "CapturedMailbox")) {
    g_captured_mailbox = value != 0;
    return true;
  }
  if (!strcmp(name, "CMHCScale")) {
    g_cmhc_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "FollowPV")) {
    g_follow_pv = value != 0;
    return true;
  }
  if (!strcmp(name, "RFPBadNode")) {
    g_rfp_badnode = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "FutSpareQuiet")) {
    g_fut_spare_quiet = value != 0;
    return true;
  }
  if (!strcmp(name, "QSDeltaBestCase")) {
    g_qs_delta_bestcase = value != 0;
    return true;
  }
  if (!strcmp(name, "QSBCMargin")) {
    g_qs_bc_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "ProbCutMargin")) {
    g_probcut_margin = value;
    return true;
  }
  if (!strcmp(name, "ProbCutImprove")) {
    g_probcut_improve = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CorrCap")) {
    g_corr_cap = value;
    return true;
  }
  if (!strcmp(name, "CorrLearnDiv")) {
    g_corr_lr_div = value;
    return true;
  }
  if (!strcmp(name, "CorrAsym")) {
    g_corr_asym = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "CorrContWeight")) {
    g_corr_cont_weight = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CorrNonPawnWeight")) {
    g_corr_np_weight = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CorrMaterial")) {
    g_corr_material = value != 0;
    return true;
  }
  if (!strcmp(name, "CorrMaterialWeight")) {
    g_corr_material_weight = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TransCorr")) {
    g_corr_trans_weight = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "ContHistDiv")) {
    g_conthist_red_div = value;
    return true;
  }
  if (!strcmp(name, "HistPruneMargin")) {
    g_histprune_margin = value;
    return true;
  }
  if (!strcmp(name, "LmrDepthPrune")) {
    g_lmrdepth_prune = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LmrDepthHistDiv")) {
    g_lmrdepth_histdiv = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "ContHistPruneDepth")) {
    g_conthist_prune_depth = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "SEECaptureMargin")) {
    g_see_cap_margin = value;
    return true;
  }
  if (!strcmp(name, "BadCapSkipAfter")) {
    g_badcap_skip_after = value < 0 ? 0 : (value > 2 ? 2 : value);
    return true;
  }
  if (!strcmp(name, "SEEQuietMargin")) {
    g_see_quiet_margin = value;
    return true;
  }
  if (!strcmp(name, "CaptureFutility")) {
    g_cap_futility = value != 0;
    return true;
  }
  if (!strcmp(name, "CapFutBase")) {
    g_capfut_base = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CapFutMult")) {
    g_capfut_mult = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CapFutChist")) {
    g_capfut_chist = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CapFutDepth")) {
    g_capfut_depth = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "CapFutVicScale")) {
    g_capfut_vic_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "QFutVicScale")) {
    g_qfut_vic_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "OppWorsening")) {
    g_opp_worsening = value != 0;
    return true;
  }
  if (!strcmp(name, "OppWorseMargin")) {
    g_opp_worse_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TripleExt")) {
    g_triple_ext = value != 0;
    return true;
  }
  if (!strcmp(name, "SingularTripleMargin")) {
    g_singular_tmargin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "NegExtTT")) {
    g_negext_tt = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "NegExtCut")) {
    g_negext_cut = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CutNodeProp")) {
    g_cutnode_prop = value != 0;
    return true;
  }
  if (!strcmp(name, "LMRNegative")) {
    g_lmr_negative = value != 0;
    return true;
  }
  if (!strcmp(name, "TTCutExact")) {
    g_ttcut_exact = value != 0;
    return true;
  }
  if (!strcmp(name, "PromoQS")) {
    g_promo_qs = value < 0 ? 0 : (value > 7 ? 7 : value);
    return true;
  }
  if (!strcmp(name, "CorrValMargin")) {
    g_corrval_margin = value != 0;
    return true;
  }
  if (!strcmp(name, "CorrValRFP")) {
    g_corrval_rfp = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CorrValExt")) {
    g_corrval_ext = value != 0;
    return true;
  }
  if (!strcmp(name, "CorrValFut")) {
    g_corrval_fut = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CorrValSee")) {
    g_corrval_see = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CorrValLmr")) {
    g_corrval_lmr = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "IIRMinDepth")) {
    g_iir_min_depth = value < 1 ? 1 : value;
    return true;
  } // gate strutturali
  if (!strcmp(name, "IIRAmount")) {
    g_iir_amount = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "IIRNotInCheck")) {
    g_iir_not_in_check = value != 0;
    return true;
  }
  if (!strcmp(name, "ProbCutMinDepth")) {
    g_probcut_min_depth = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "ProbCutDepthOff")) {
    g_probcut_depth_off = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "SingularTTMargin")) {
    g_singular_ttmargin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "SingularDepthDiv")) {
    g_singular_depth_div = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "MalusScaled")) {
    g_malus_scaled = value != 0;
    return true;
  }
  if (!strcmp(name, "MalusScaleCoef")) {
    g_malus_scale_coef = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "MalusQuad")) {
    g_malus_quad = value != 0;
    return true;
  }
  if (!strcmp(name, "MalusQuadCoef")) {
    g_malus_quad_coef = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "RazorTTQuiet")) {
    g_razor_ttquiet = value != 0;
    return true;
  }
  if (!strcmp(name, "RazorTTLower")) {
    g_razor_ttlower = value != 0;
    return true;
  }
  if (!strcmp(name, "SingularExactMargin")) {
    g_singular_exact_margin = value != 0;
    return true;
  }
  if (!strcmp(name, "SingularExactDecouple")) {
    g_singular_exact_decouple = value != 0;
    return true;
  }
  if (!strcmp(name, "SingularExactMaxDepth")) {
    g_singular_exact_maxdepth = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "DoDeeper")) {
    g_do_deeper = value != 0;
    return true;
  }
  if (!strcmp(name, "DoDeeperBase")) {
    g_dodeeper_base = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "HindsightExt")) {
    g_hindsight_ext = value != 0;
    return true;
  }
  if (!strcmp(name, "HindsightMargin")) {
    g_hindsight_margin = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "HindsightRed")) {
    g_hindsight_red = value != 0;
    return true;
  }
  if (!strcmp(name, "HindsightRedMargin")) {
    g_hindsight_red_margin = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "HindsightRedThresh")) {
    g_hindsight_red_thresh = value;
    return true;
  }
  if (!strcmp(name, "DoShallowerMargin")) {
    g_doshallower_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "BadNoisy")) {
    g_bad_noisy = value != 0;
    return true;
  }
  if (!strcmp(name, "BadNoisyCount")) {
    g_bad_noisy_count = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "LMREnrich")) {
    g_lmr_enrich = value != 0;
    return true;
  }
  if (!strcmp(name, "LMREnrichAmount")) {
    g_lmr_enrich_amount = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "RazorQuadCoef")) {
    g_razor_quad_coef = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "RFPDepth")) {
    g_rfp_depth_cap = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "RazorDepth")) {
    g_razor_depth_cap = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "IID")) {
    g_iid = value != 0;
    return true;
  }
  if (!strcmp(name, "IIDDepth")) {
    g_iid_depth = value < 2 ? 2 : value;
    return true;
  }
  if (!strcmp(name, "IIDReduction")) {
    g_iid_reduction = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "LMRFine")) {
    g_lmr_fine = value != 0;
    return true;
  }
  if (!strcmp(name, "LMRFCut")) {
    g_lmrf_cut = value;
    return true;
  }
  if (!strcmp(name, "LMRFCutNoTT")) {
    g_lmrf_cut_nott = value;
    return true;
  }
  if (!strcmp(name, "LMRFTTCap")) {
    g_lmrf_ttcap = value;
    return true;
  }
  if (!strcmp(name, "LMRFTTPv")) {
    g_lmrf_ttpv = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRFPv")) {
    g_lmrf_pv = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRFSS")) {
    g_lmrf_ss = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRFCorr")) {
    g_lmrf_corr = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRFAll")) {
    g_lmrf_all = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRFImprov")) {
    g_lmrf_improv = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRFEvalCut")) {
    g_lmrf_evalcut = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TTCutDepthOverride")) { g_ttcut_depth_override = value < 0 ? 0 : value; return true; }
  if (!strcmp(name, "TTFailLowMove")) { g_tt_faillow_move = value != 0; return true; }
  if (!strcmp(name, "TTPvInherit")) { g_ttpv_inherit = value != 0; return true; }
  if (!strcmp(name, "NMPCutNodeOnly")) { g_nmp_cutnode_only = value != 0; return true; }
  if (!strcmp(name, "IIRNoAllNode")) { g_iir_no_allnode = value != 0; return true; }
  if (!strcmp(name, "ProbCutTTAll")) { g_probcut_tt_all = value < 0 ? 0 : (value > 3 ? 3 : value); return true; }
  if (!strcmp(name, "RFPTTMoveGate")) { g_rfp_ttmove_gate = value != 0; return true; }
  if (!strcmp(name, "CorrFailLowAll")) { g_corr_faillow_all = value < 0 ? 0 : (value > 2 ? 2 : value); return true; }
  if (!strcmp(name, "AlphaDepthDecAmt")) { g_alpha_depth_dec_amt = value < 1 ? 1 : (value > 6 ? 6 : value); return true; }
  if (!strcmp(name, "LmrAlphaGap")) {
    g_lmr_alpha_gap = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LmrAlphaLo")) {
    g_lmr_alpha_lo = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LmrAlphaHi")) {
    g_lmr_alpha_hi = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRFCutoff")) {
    g_lmrf_cutoff = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRFCont4")) {
    g_lmrf_cont4 = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRExpect")) {
    g_lmr_expect = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "OptPerThread")) {
    g_opt_per_thread = value != 0;
    return true;
  }
  if (!strcmp(name, "EvalOptimism")) {
    g_eval_optimism = value != 0;
    if (!g_eval_optimism) {
      g_optimism[0] = g_optimism[1] = 0;
    }
    return true;
  }
  if (!strcmp(name, "EvalOptStrength")) {
    g_opt_strength = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "EvalOptDiv")) {
    g_opt_div = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "SeeGE")) {
    g_see_ge = (value != 0);
    return true;
  }
  if (!strcmp(name, "SeeGEVerify")) {
    g_see_ge_verify = (value != 0);
    if (value) {
      g_seege_checks = 0;
      g_seege_bad = 0;
    } else
      seege_verify_report();
    return true;
  }
  if (!strcmp(name, "QSDeltaMargin")) {
    g_qs_delta = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "SingularMarginPD")) {
    g_singular_mpd = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TMDropThresh")) {
    g_tm_drop_thresh = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TMDropCap")) {
    g_tm_drop_cap = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "NodeTMBase")) {
    g_nodetm_base = value < 100 ? 100 : value;
    return true;
  }
  if (!strcmp(name, "NodeTMMin")) {
    g_nodetm_min = value < 10 ? 10 : (value > 100 ? 100 : value);
    return true;
  }
  if (!strcmp(name, "LMRMinDepth")) {
    g_lmr_min_depth = value < 2 ? 2 : value;
    return true;
  }
  if (!strcmp(name, "LMRMinMoves")) {
    g_lmr_min_moves = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "LMRCaptures")) {
    g_lmr_captures = value != 0;
    return true;
  }
  if (!strcmp(name, "LMRCapScale")) {
    g_lmr_cap_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRCheckExempt")) {
    g_lmr_check_exempt = value < 0 ? 0 : (value > 2 ? 2 : value);
    return true;
  }
  if (!strcmp(name, "TMMovesToGo")) {
    g_tm_movestogo = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TMIncFrac")) {
    g_tm_inc_frac = value;
    return true;
  }
  if (!strcmp(name, "TMMaxMult")) {
    g_tm_max_mult = value < 100 ? 100 : value;
    return true;
  }
  if (!strcmp(name, "TMInstab")) {
    g_tm_instab = value;
    return true;
  }
  if (!strcmp(name, "TMDropDiv")) {
    g_tm_drop_div = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TMStability")) {
    g_tm_stability = value != 0;
    return true;
  }
  if (!strcmp(name, "RootMateRestore")) {
    g_root_mate_restore = value != 0;
    return true;
  }
  if (!strcmp(name, "LMRDecisiveBeta")) {
    g_lmr_decisive_beta = value != 0;
    return true;
  }
  if (!strcmp(name, "TTDecisiveClamp")) {
    g_tt_decisive_clamp = value != 0;
    return true;
  }
  if (!strcmp(name, "TTSecondaryAge")) {
    g_tt_secondary_age = value != 0;
    return true;
  }
  if (!strcmp(name, "SEEStalemateGuard")) {
    g_see_stalemate_guard = value != 0;
    return true;
  }
  if (!strcmp(name, "TBScoreTT")) {
    g_tb_tt_store = value != 0;
    return true;
  }
  if (!strcmp(name, "ProbCutImproving")) {
    g_probcut_improving = value != 0;
    return true;
  }
  if (!strcmp(name, "QSCaptHist")) {
    g_qs_capthist = value != 0;
    return true;
  }
  if (!strcmp(name, "QSCaptHistScale")) {
    g_qs_capthist_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "NodeCache")) {
    g_node_cache = value != 0;
    return true;
  }
  if (!strcmp(name, "NodeCacheBonus")) {
    g_nc_bonus = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "NodeCacheMinSum")) {
    g_nc_min_sum = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "HistBonusMoves")) {
    g_hist_bonus_moves = value < 0 ? 0 : value;
    return true;
  } // Q-18
  if (!strcmp(name, "HistAge")) {
    g_hist_age = value < 1 ? 1 : (value > 1024 ? 1024 : value);
    return true;
  } // Q-25
  if (!strcmp(name, "HistInitQuiet")) {
    g_hist_init_quiet = value < 0 ? 0 : value;
    return true;
  } // Q-26
  if (!strcmp(name, "HistInitCapt")) {
    g_hist_init_capt = value < 0 ? 0 : value;
    return true;
  } // Q-26
  if (!strcmp(name, "LMRFPly")) {
    g_lmrf_ply = value < 0 ? 0 : value;
    return true;
  } // Q-19a
  if (!strcmp(name, "LMRFKiller")) {
    g_lmrf_killer = value < 0 ? 0 : value;
    return true;
  } // Q-19b
  if (!strcmp(name, "RecaptureExt")) {
    g_recapture_ext = value != 0;
    return true;
  }
  if (!strcmp(name, "RecaptureTension")) {
    g_recapture_tension = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TMStabThresh")) {
    g_tm_stab_thresh = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TMStabStep")) {
    g_tm_stab_step = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TMStabFloor")) {
    g_tm_stab_floor = value < 10 ? 10 : (value > 100 ? 100 : value);
    return true;
  }
  // ---- TM v2 (quarto audit Q-01..Q-08) ----
  if (!strcmp(name, "TMv2")) {
    g_tmv2 = value != 0;
    return true;
  }
  if (!strcmp(name, "TMv2Stab0")) {
    g_tmv2_stab[0] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Stab1")) {
    g_tmv2_stab[1] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Stab2")) {
    g_tmv2_stab[2] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Stab3")) {
    g_tmv2_stab[3] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Stab4")) {
    g_tmv2_stab[4] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Eval0")) {
    g_tmv2_eval[0] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Eval1")) {
    g_tmv2_eval[1] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Eval2")) {
    g_tmv2_eval[2] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Eval3")) {
    g_tmv2_eval[3] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Eval4")) {
    g_tmv2_eval[4] = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2EvalWindow")) {
    g_tmv2_eval_window = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TMv2EvalPrevAvg")) {
    g_tmv2_eval_prevavg = value != 0;
    return true;
  }
  if (!strcmp(name, "TMv2NodeBase")) {
    g_tmv2_node_base = value < 100 ? 100 : value;
    return true;
  }
  if (!strcmp(name, "TMv2NodeMult")) {
    g_tmv2_node_mult = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2NodeMin")) {
    g_tmv2_node_min = value < 10 ? 10 : (value > 100 ? 100 : value);
    return true;
  }
  if (!strcmp(name, "TMv2NodeMax")) {
    g_tmv2_node_max = value < 100 ? 100 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Alloc")) {
    g_tmv2_alloc = value != 0;
    return true;
  }
  if (!strcmp(name, "TMv2MtgBase")) {
    g_tmv2_mtg_base = value < 2 ? 2 : value;
    return true;
  }
  if (!strcmp(name, "TMv2MtgSlope")) {
    g_tmv2_mtg_slope = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TMv2MtgMin")) {
    g_tmv2_mtg_min = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TMv2OptPct")) {
    g_tmv2_opt_pct = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "TMv2Predicted")) {
    g_tmv2_pred = value != 0;
    if (!g_tmv2_pred)
      g_tm_pred_hash = 0;
    return true;
  }
  if (!strcmp(name, "TMPredBestThread")) {
    g_tm_pred_best_thread = value != 0;
    return true;
  }
  if (!strcmp(name, "TMv2PredHit")) {
    g_tmv2_pred_hit = value < 100 ? 100 : value;
    return true;
  }
  if (!strcmp(name, "TMv2PredMiss")) {
    g_tmv2_pred_miss = value < 100 ? 100 : value;
    return true;
  }
  if (!strcmp(name, "TMv2RootSingular")) {
    g_tmv2_rsing = value != 0;
    return true;
  }
  if (!strcmp(name, "TMv2RSMarginBase")) {
    g_tmv2_rs_margin_base = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TMv2RSMarginMin")) {
    g_tmv2_rs_margin_min = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TMv2RSMarginSlope")) {
    g_tmv2_rs_margin_slope = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TMv2RSTimeFrac")) {
    g_tmv2_rs_timefrac = value < 0 ? 0 : (value > 100 ? 100 : value);
    return true;
  }
  if (!strcmp(name, "TMv2RSScoreCap")) {
    g_tmv2_rs_scorecap = value < 100 ? 100 : value;
    return true;
  }
  if (!strcmp(name, "TMv2SingleReply")) {
    g_tmv2_single_reply = value != 0;
    return true;
  }
  if (!strcmp(name, "TMv2MateStop")) {
    g_tmv2_mate_stop = value != 0;
    return true;
  }
  if (!strcmp(name, "TMv2MateIters")) {
    g_tmv2_mate_iters = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TTCutBonusScale")) {
    g_ttcut_bonus_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "NMPVerifDepth")) {
    g_nmp_verif_depth = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "LMPCheckGuard")) {
    g_lmp_check_guard = value != 0;
    return true;
  }
  if (!strcmp(name, "LMPBase")) {
    g_lmp_base = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMPQuad")) {
    g_lmp_quad = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CheckExtDepth")) {
    g_check_ext_depth = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LMRStatScoreDiv")) {
    g_lmr_ss_div = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "LMRStatScoreOffset")) {
    g_lmr_ss_offset = value;
    return true;
  }
  if (!strcmp(name, "LMRContHistDiv")) {
    g_lmr_ch_div = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "CutNodeLMRExtra")) {
    g_cutnode_lmr_extra = value;
    return true;
  }
  if (!strcmp(name, "TTPvAmount")) {
    g_ttpv_amount = value < 0 ? 0 : (value > 2 ? 2 : value);
    return true;
  }
  if (!strcmp(name, "TTEvalNoDecay")) {
    g_tt_eval_no_decay = value != 0;
    return true;
  }
  if (!strcmp(name, "ThreatScale")) {
    g_threat_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "ThreatHistWeight")) {
    g_threat_hist_weight = value < 0 ? 0 : value;
    return true;
  }
  // FailHighSmooth: i tre pesi separati (scala /1024, 512 = midpoint storico).
  if (!strcmp(name, "FHTQsStandPat")) {
    g_fh_t_qs_standpat = value;
    return true;
  }
  if (!strcmp(name, "FHTQsFinal")) {
    g_fh_t_qs_final = value;
    return true;
  }
  if (!strcmp(name, "FHTRfp")) {
    g_fh_t_rfp = value;
    return true;
  }
  if (!strcmp(name, "CheckBonus")) {
    g_check_bonus = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "QuietOffense")) {
    g_quiet_offense = value != 0;
    return true;
  }
  if (!strcmp(name, "OffenseBonus")) {
    g_offense_bonus = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "WallPawnPenalty")) {
    g_wallpawn_penalty = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "ContHist36Weight")) {
    g_conthist36_weight = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PriorBonusScale")) {
    g_prior_bonus_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PriorBonusFactorScale")) {
    g_pb_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PBBase")) {
    g_pb_base = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PBMoveCount")) {
    g_pb_mc = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PBMoveCountCap")) {
    g_pb_mc_cap = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PBTTMove")) {
    g_pb_ttm = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PBEval")) {
    g_pb_ev = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PBEvalMargin")) {
    g_pb_ev_marg = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PBSwing")) {
    g_pb_swing = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PBSwingMargin")) {
    g_pb_swing_marg = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "LowPlyWeight")) {
    g_lowply_weight = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "StatEvalDiffMult")) {
    g_seo_mult = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "CutoffCntPenalty")) {
    g_cutoffcnt_penalty = value < 0 ? 0 : value;
    return true;
  }
  // F-018 (secondo audit): micro-tecniche Obsidian/Berserk, default OFF/neutro.
  if (!strcmp(name, "PostLMRHist")) {
    g_postlmr_hist = value != 0;
    return true;
  }
  if (!strcmp(name, "PostLMRHistScale")) {
    g_postlmr_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TTCutRefine")) {
    g_ttcut_refine = value != 0;
    return true;
  }
  if (!strcmp(name, "TTResearch")) {
    g_tt_research = value != 0;
    return true;
  }
  // Idee da Coda (27/09/2026), vedi 02_params_candidates.inc
  if (!strcmp(name, "RDRKnee")) {
    g_rdr_knee = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "RDRRfp")) {
    g_rdr_rfp = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "RDRLmp")) {
    g_rdr_lmp = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "RDRProbCut")) {
    g_rdr_probcut = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TTNearMiss")) {
    g_tt_nearmiss = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TTDamp")) {
    g_tt_damp = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TTResearchMargin")) {
    g_tt_research_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "TTCutFifty")) {
    g_ttcut_fifty = value < 1 ? 1 : value;
    return true;
  }
  if (!strcmp(name, "TTCutMalus")) {
    g_ttcut_malus = value != 0;
    return true;
  }
  if (!strcmp(name, "TTCutMalusSeen")) {
    g_ttcut_malus_seen = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "GoodCapHistDiv")) {
    g_goodcap_hist_div = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "AspAvg")) {
    g_asp_avg = value != 0;
    return true;
  }
  if (!strcmp(name, "AspFHRed")) {
    g_asp_fhred = value != 0;
    return true;
  }
  if (!strcmp(name, "SingularMinDepth")) {
    g_singular_mindepth = value < 4 ? 4 : value;
    return true;
  }
  if (!strcmp(name, "SingularDECap")) {
    g_singular_de_cap = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "SingularPlyGuard")) {
    g_singular_plyguard = value != 0;
    return true;
  }
  if (!strcmp(name, "NegExtAlpha")) {
    g_negext_alpha = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "FailHighSmooth")) {
    g_fh_smooth = value != 0;
    return true;
  }
  if (!strcmp(name, "NMPStaticMargin")) {
    g_nmp_static_margin = value != 0;
    return true;
  }
  if (!strcmp(name, "NMPStaticMult")) {
    g_nmp_sm_mult = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "NMPStaticBias")) {
    g_nmp_sm_bias = value;
    return true;
  }
  if (!strcmp(name, "NMPTTNoisy")) {
    g_nmp_ttnoisy = value != 0;
    return true;
  }
  if (!strcmp(name, "AlphaDepthDec")) {
    g_alpha_depth_dec = value != 0;
    return true;
  }
  if (!strcmp(name, "FHBoostMargin")) {
    g_fhboost_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "HistTrivGuard")) {
    g_hist_triv_guard = value != 0;
    return true;
  }
  if (!strcmp(name, "MalusPct")) {
    g_malus_pct = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "EasyCapGate")) {
    g_easycap_gate = value != 0;
    return true;
  }
  if (!strcmp(name, "RFPHistThresh")) {
    g_rfp_hist_thresh = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "KillerReset")) {
    g_killer_reset = value != 0;
    return true;
  }
  if (!strcmp(name, "CounterMove")) {
    g_countermove = value != 0;
    return true;
  }
  if (!strcmp(name, "QSMoveCap")) {
    g_qs_move_cap = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "QSCapRecap")) {
    g_qs_cap_recap = value > 0 ? 1 : 0;
    return true;
  }
  if (!strcmp(name, "QSDrawCheck")) {
    g_qs_draw_check = value != 0;
    return true;
  }
  if (!strcmp(name, "EPKeyFix")) {
    g_ep_key_fix = value != 0;
    return true;
  }
  if (!strcmp(name, "CutoffStats")) {
    g_cutoff_stats = value != 0;
    return true;
  }
  if (!strcmp(name, "EvalTTWrite")) {
    g_eval_tt_write = value != 0;
    return true;
  }
  if (!strcmp(name, "TTTwoLevel")) {
    g_tt_twolevel = value != 0;
    return true;
  }
  if (!strcmp(name, "TTMoveKeep")) {
    g_tt_move_keep = value != 0;
    return true;
  }
  if (!strcmp(name, "TTKeepMargin")) {
    g_tt_keep_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "ProbCutInCheckMargin")) {
    g_probcut_incheck_margin = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "PawnHistoryWeight")) {
    g_pawn_hist_weight = value;
    return true;
  }
  if (!strcmp(name, "MainHistWeight")) {
    g_mainhist_weight = value < 1 ? 1 : value;
    return true;
  } // /100
  if (!strcmp(name, "ContHistWeight")) {
    g_conthist_weight = value < 1 ? 1 : value;
    return true;
  } // /100
  if (!strcmp(name, "LMPScale")) {
    g_lmp_scale = value < 10 ? 10 : value;
    return true;
  }
  if (!strcmp(name, "AggrLMRDiv")) {
    g_aggr_lmr_div = value;
    return true;
  }
  if (!strcmp(name, "AggrLMRClamp")) {
    g_aggr_lmr_clamp = value;
    return true;
  }
  if (!strcmp(name, "NMPBase")) {
    g_nmp_base = value;
    return true;
  }
  if (!strcmp(name, "NMPDiv")) {
    g_nmp_div = value;
    return true;
  }
  if (!strcmp(name, "LMREvalMargin")) {
    g_lmr_eval_margin = value;
    return true;
  }
  if (!strcmp(name, "LMRTTDepth")) {
    g_lmr_ttdepth = value;
    return true;
  }
  if (!strcmp(name, "LMRBase")) {
    g_lmr_base_x100 = value;
    init_lmr_table();
    return true;
  }
  if (!strcmp(name, "LMRDeepK")) {
    g_lmr_deep_k = value;
    init_lmr_table();
    return true;
  }
  if (!strcmp(name, "LMRDeepD0")) {
    g_lmr_deep_d0 = value < 2 ? 2 : value;
    init_lmr_table();
    return true;
  }
  if (!strcmp(name, "LMRSFBase")) {
    g_lmr_sf_base = value != 0;
    return true;
  }
  if (!strcmp(name, "LMRSFMult")) {
    g_lmr_sf_mult = value;
    init_lmr_table();
    return true;
  }
  if (!strcmp(name, "LMRSFOff")) {
    g_lmr_sf_off = value;
    init_lmr_table();
    return true;
  }
  if (!strcmp(name, "LMRDiv")) {
    g_lmr_div_x100 = value;
    init_lmr_table();
    return true;
  }
  if (!strcmp(name, "DiverseSMPAmount")) {
    g_diverse_smp_amount = value;
    return true;
  }
  if (!strcmp(name, "DiverseSMPFine")) {
    g_diverse_smp_fine = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "MulticutCorr")) {
    g_multicut_corr = value != 0;
    return true;
  }
  if (!strcmp(name, "MulticutCorrScale")) {
    g_multicut_corr_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "MulticutTTMalus")) {
    g_multicut_ttmalus = value != 0;
    return true;
  }
  if (!strcmp(name, "MulticutTTMalusScale")) {
    g_multicut_ttmalus_scale = value < 0 ? 0 : value;
    return true;
  }
  if (!strcmp(name, "NMPEvalDiv")) {
    g_nmp_eval_div = value;
    return true;
  }
  if (!strcmp(name, "QFutMargin")) {
    g_qfut_margin = value;
    return true;
  }
  if (!strcmp(name, "HistBonusMult")) {
    g_hist_bonus_mult = value;
    return true;
  }
  if (!strcmp(name, "HistBonusSub")) {
    g_hist_bonus_sub = value;
    return true;
  }
  if (!strcmp(name, "HistBonusMax")) {
    g_hist_bonus_max = value;
    return true;
  }
  if (!strcmp(name, "CaptureHistDiv")) {
    g_caphist_div = value < 1 ? 1 : value;
    return true;
  }
  return false;
}

// ---- Self-play data logging (Phase 1: policy-net training dataset) ----------
// When enabled, each completed root search appends one TSV record:
//   <FEN> \t <bestmove-uci> \t <score-cp (side-to-move relative)> \t <depth>
// Toggled via the DataLog / DataFile UCI options. The actual output file gets a
// ".<pid>" suffix per process, so multiple parallel self-play instances never
// collide; merge them afterwards (e.g. cat triumviratus_dataset.txt.* >
// all.txt).
static bool g_data_log_enabled = false;
static std::string g_data_log_file = "triumviratus_dataset.txt";

void set_data_log_enabled(bool enabled) { g_data_log_enabled = enabled; }
