// search/02_params_candidates.inc -- Candidati e parametri di tuning (toggle, margini, LMR fine, TM).
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ===========================================================================
// CANDIDATI "notte" (2026-06-04) — ognuno dietro toggle, default OFF =
// byte-identico al baseline (zero rischio al motore). Da SPRT-are UNO ALLA
// VOLTA (vedi i .bat in Tuning_SPSA/night/). Dato che "il search è spremuto",
// aspettativa media ≤0, ma col toggle OFF non toccano nulla finché non li
// provi.
// ===========================================================================

// (1) NMPEvalScale: aumenta la riduzione del null-move quando static_eval
// supera
//     beta di molto (più sopra beta = potatura più aggressiva). SF-standard.
static bool g_nmp_eval_scale = false;
void set_nmp_eval_scale(bool v) { g_nmp_eval_scale = v; }
// 3/08/2026: allineato alla forma che SF ha fatto PASSARE (356d7c5c),
//   R = 7 + depth/3 + max((ss->staticEval - beta) / 256, 0)
// Prima era /100 CON cap a 3 e sulla `eval` corretta dalla TT: cioe' la variante
// che SF aveva REVERTATO per mate-finding dopo 290k partite verdi.
int g_nmp_eval_div = 256;

// (2) RFPDepth8: estende la reverse-futility da depth<=6 a depth<=8.
static bool g_rfp_depth8 = false;
void set_rfp_depth8(bool v) { g_rfp_depth8 = v; }

// (3) RazorDepth4: estende il razoring da depth<=3 a depth<=4.
static bool g_razor_depth4 = false;
void set_razor_depth4(bool v) { g_razor_depth4 = v; }

// (4) QFutility: futility per-mossa in quiescence — salta le catture che non
//     possono alzare best_score fino ad alpha nemmeno incassando il pezzo
//     catturato (escluse ep/promo per sicurezza). Margine = QFutMargin.
static bool g_qfutility = false;
void set_qfutility(bool v) { g_qfutility = v; }
int g_qfut_margin = 199;
// QFutVicScale (2026-07-24): ponte cp->eval-net del termine-vittima, /10000
// INSIEME a EvalScale — gemello di g_capfut_vic_scale. Sostituisce il vecchio
// magic-number "* 3" hardcoded (tarato a EvalScale=56, mai ri-seguito): 500 a
// EvalScale=60 = 500*60/10000 = ESATTAMENTE 3.0 (byte-identico oggi) ma
// auto-ricalibrante ai bake futuri della rete. Deliberatamente > del capfut
// (2.35): qfut sovrastima per potare MENO le catture grosse (errore sicuro).
// Spin co-tunabile: entra nel co-tune B1-catture insieme a CapFutVicScale.
int g_qfut_vic_scale = 500;

// (5) HistBonusSF: bonus history lineare-clampato min(mult*d - sub, max) invece
//     di depth*depth. Cambia solo la MAGNITUDINE di bonus/malus della history.
//     **BAKED ON (2026-06-04): +24.06 Elo LOS 99.99% @810 (8+0.08, 1t).** Primo
//     +Elo di ricerca dopo una settimana. Toggle conservato per A/B (off =
//     depth*depth).
static bool g_hist_bonus_sf = true;
void set_hist_bonus_sf(bool v) { g_hist_bonus_sf = v; }
int g_hist_bonus_mult =
    490; // [4.1 BAKE 282->326] SPSA co-tune history/LMR @12+0.12 (+5-7 Elo)
int g_hist_bonus_sub = 299;  // [4.1 BAKE 59->35]
int g_hist_bonus_max = 2946; // [4.1 BAKE 1247->2439]

// CaptureHist (UCI option "CaptureHist"). Capture history: tabella
// [piece][to][victim] che impara quali catture producono cutoff, e ne bias-a
// l'ordinamento (sia good che bad captures, main search + qsearch). Default ON,
// con DUE fix (2026-06-04) che hanno tolto una REGRESSIONE da ~-15/-21 Elo che
// il motore spediva:
//   (1) SCALING: il caphist era sommato GREZZO a mvv_lva (range +/-7000 vs
//   100-605
//       -> schiacciava l'MVV-LVA ~10x). `CaptureHistDiv` default 16 lo riporta
//       a
//       +/-437 ~ mvv_lva (= scala SF (mvv*7+capthist)/16). A/B div1 ON vs OFF =
//       -21; div16 (solo scaling) = -12.
//   (2) MALUS: il malus alle catture provate-e-fallite era applicato solo
//   quando il
//       cutoff era causato da una cattura; SF lo applica a OGNI cutoff (anche
//       best quiet). Vedi il loop nell'update. Con (1)+(2): div16+fix ON vs OFF
//       = ~0 (neutro, -0.3@1098 e +0.8@458) -> regressione rimossa.
// Toggle conservato per A/B; OFF = contributo 0 + niente update (tabella a
// zero). `CaptureHistDiv` resta una leva SPSA (8/16/24) per un futuro tentativo
// di +Elo.
static bool g_capture_hist = true;
void set_capture_hist(bool v) { g_capture_hist = v; }
int g_caphist_div = 21; // REVERT 2026-07-23: bake SPSA B1 (15) evaporato a 4452 games (+1.64 LOS73%, LLR piatto) -> torna al default

// Lazy SMP is the ONLY parallel-search scheme (ADOPTED, +55 Elo @4CPU; direct
// A/B
// @2+0.02 4-thread was +102 Elo, LOS 99.99%). Helper threads search fully
// independently (shared TT only) and diversify via per-thread depth skipping
// (see LSMP_Skip* below). The legacy ABDADA busy-node coordination was removed.

// 4-way set-associative TT on/off (UCI option "TT4Way"). Default OFF reproduces
// the direct-mapped table; defined here, declared extern in tt.h (the bucket
// logic lives in tt.h). Matters more now that Lazy SMP has many threads
// hammering the TT. Toggle for a clean A/B.
bool g_tt_4way = false;
void set_tt_4way(bool v) { g_tt_4way = v; }
bool g_tt_twolevel =
    true; // 5.1 BAKE ON (TT2L on vs off: +4.55 LOS 86%% @1758g 12+0.12, mai
          // neg, -4%% nodi). bucket 2-slot depth-preferred + always-replace
bool g_large_pages = true; // LargePages: TT su large pages 2MB (default ON).
                           // OFF = new[] (baseline) per A/B NPS

// Sentinella "nessuna static eval a questo ply" per eval_stack (FIX P0.6):
// scritta ai nodi in scacco quando LazyEval salta la NNUE. Fuori dal range eval
// reale.
#define EVAL_NONE (-32100)

// TTEvalImprove (UCI "TTEvalImprove", default ON — P1.1 2026-06-09): usa
// tt_score (se il bound e' coerente) al posto della static eval nelle decisioni
// di pruning. E' informazione di qualita'-search gia' pagata: SF lo fa da
// sempre. Toggle per A/B.
static bool g_tt_eval_improve = true;
void set_tt_eval_improve(bool v) { g_tt_eval_improve = v; }

// UpcomingRep (UCI "UpcomingRep", default ON — P1.2 2026-06-09): rilevazione
// "ripetizione imminente" (cuckoo, Kervinck/SF has_game_cycle): ai nodi
// non-root con alpha < 0, se esiste una mossa reversibile che riporta in una
// posizione gia' vista, alza alpha a 0 (lato anti-shuffling). Tabelle cuckoo
// inizializzate in init_threads.
static bool g_upcoming_rep = true;
void set_upcoming_rep(bool v) { g_upcoming_rep = v; }

// ---- TOGGLE DI ABLAZIONE dei fix 3.8 (night tournament, 2026-06-09)
// ---------- Ogni fix P0 incondizionato della 3.8 e' gated da un toggle default
// ON: UN solo exe isola ogni fix via fastchess `option.X=false`. Tutti OFF (+
// TTEvalImprove e UpcomingRep OFF) = search-identico alla 3.7 (node-identity
// verificabile).
bool g_ttmove24 = true; // P0.1: TT move 24 bit (OFF = troncamento 21-bit
                        // come 3.7). extern in tt.h.
void set_ttmove24(bool v) { g_ttmove24 = v; }
int g_tt_keep_margin = 0; // TTKeepMargin, vedi store_tt in tt.h (0 = regola storica)
bool g_tt_move_keep =
    true; // TTMoveKeep (default off = byte-identico): conserva la TT move sui
          // store fail-low senza mossa (SF). Alza ttrate ai cut-node. extern in
          // tt.h.
bool g_see_fix = true; // P0.2: SEE sui quiet + casa e.p. corretta (OFF = SEE=0
                       // sui quiet). extern in see.cpp.
void set_see_fix(bool v) { g_see_fix = v; }
static bool g_killer_lmr_fix =
    true; // P0.3: sconto LMR killer al ply del nodo (OFF = nessuno sconto, come
          // il dead-code 3.7)
void set_killer_lmr_fix(bool v) { g_killer_lmr_fix = v; }
static bool g_qsearch_corr = true; // P0.4: corr-history sullo stand-pat di
                                   // qsearch (OFF = solo nodi interni)
void set_qsearch_corr(bool v) { g_qsearch_corr = v; }
static bool g_improving_fix = true; // P0.6: sentinel EVAL_NONE + fallback ply-4
                                    // (OFF = confronto col vecchio 0)
void set_improving_fix(bool v) { g_improving_fix = v; }

// Lazy eval on/off (UCI option "LazyEval"). Default ON (CONFIRMED: combined
// with TimeMgmt, +9.1 Elo LOS 96% @1+0.01, estimate held over 3k+ games). Skips
// the NNUE static eval in nodes where the side to move is in check: there the
// static eval is not used by any forward-pruning rule (all gated !in_check), so
// computing it is wasted work -> a few % NPS for free.
static bool g_lazy_eval = true;
void set_lazy_eval(bool v) { g_lazy_eval = v; }

// Extra time management on/off (UCI option "TimeMgmt"). Default ON (CONFIRMED
// with LazyEval, +9.1 Elo LOS 96%). The main thread grants extra time when the
// root score DROPS vs the previous completed iteration (the position may be
// turning against us), on top of the existing best-move-instability extension.
static bool g_time_mgmt = true;
void set_time_mgmt(bool v) { g_time_mgmt = v; }

// Time-management constants, SPSA-tunable (mai tarate; default = valori a
// mano). Statici (usati in parse_go, uci_mt.cpp): quota base =
// remaining/TMMovesToGo + inc*TMIncFrac/100; maximum = optimum*TMMaxMult/100.
// Dinamici (loop ID, sotto): instabilita' best-move += headroom*TMInstab/100;
// eval che cala += headroom*d/TMDropDiv.
int g_tm_movestogo = 27; // assunzione moves-to-go quando ignota (minore = piu'
                         // tempo/mossa) [BAKE 2026-07-03 TM post-F-003, TC15
                         // SPSA avg30: 24->27, SPRT +20.0 LOS94%]
int g_tm_inc_frac = 94;  // % dell'incremento da spendere [BAKE 2026-07-03 TM
                         // post-F-003: invariato, 94->94]
int g_tm_max_mult = 614; // maximum = optimum * questo/100 (burst su posizioni
                         // difficili) [BAKE 2026-07-03 TM post-F-003: 592->614]
int g_tm_instab = 59;    // % dell'headroom aggiunta al soft su cambio best-move
                         // [BAKE 2026-07-03 TM post-F-003: 64->59]
int g_tm_drop_div =
    508; // divisore dell'estensione su score-drop (minore = piu' tempo) [BAKE
         // 2026-07-03 TM post-F-003: 570->508]

// T-01 TMStability (default OFF): riduzione del soft-time quando la best move a
// root resta INVARIATA per piu' iterazioni consecutive (SF: bestMove stabile =
// confidenza = si spende meno). Complementare al NodeTM (che guarda i nodi di
// QUESTA iterazione): la stabilita' guarda la persistenza TRA iterazioni.
// Segnale diverso, SF usa entrambi. Curva: dopo `thresh` iterazioni stabili,
// scala la DURATA optimum di -step/1000 per ogni iterazione stabile in piu',
// con pavimento `floor`% (mai sotto). Solo main thread, timeset. Toggle via
// set_search_param (UCI check "TMStability"), come AspAvg/QSDrawCheck.
static bool g_tm_stability = false;
int g_tm_stab_thresh =
    4; // iterazioni stabili prima che la riduzione inizi (SPSA)
int g_tm_stab_step =
    40; // riduzione per-mille per ogni iterazione stabile oltre thresh (SPSA)
int g_tm_stab_floor =
    55; // % minima della durata (mai ridurre sotto questo) (SPSA)

// ---- TM v2 (quarto audit, blocco Q-01..Q-08) — TUTTO default-OFF
// ----------------- Q-01 master `TMv2`: composizione MOLTIPLICATIVA stateless
// (Alexandria/Caissa) al posto della catena additiva
// instab+drop+NodeTM(+TMStability): ogni iterazione i fattori vengono
// ricalcolati FRESCHI dalla durata base (soft_time_limit-starttime), niente
// accumulo -> niente drift, e ogni fattore e' bidirezionale (su posizioni calme
// si spende <1.0x, il risparmio si ricicla sulle mosse difficili). Il vecchio
// path resta intatto per A/B (TMv2=false).
static bool g_tmv2 = true;
// Q-02 stabilita' bestmove extend-AND-reduce in una tabella 5 punti (Alexandria
// {2.38,1.29,1.07,0.91,0.71}): indice = iterazioni consecutive con best
// invariata (cap 4). Sostituisce l'estensione binaria +59% (che era
// extend-only).
int g_tmv2_stab[5] = {232, 172, 82, 112, 56}; // %
// Q-03 trend-eval bidirezionale (bucket Alexandria): counter di iterazioni con
// score entro +-window dalla media mobile avg_score (F-018.5a), cap 4 ->
// {1.25,1.15,1.03,0.92,0.87}: eval ferma = confidenza = risparmia; eval che
// salta (in ENTRAMBE le direzioni, non solo drop) = piu' tempo.
int g_tmv2_eval[5] = {139, 119, 101, 93, 88}; // %
// 10 -> 20 il 12/08/2026, INSIEME a g_tmv2_eval_prevavg = 1. I due valori vanno
// letti come una cosa sola: col difetto, `ediff` valeva meta' e 10 significava 20;
// tolto il difetto, 20 significa 20. La soglia EFFETTIVA non cambia.
int g_tmv2_eval_window = 20;
// TMv2EvalPrevAvg — BAKATO A 1 il 12/08/2026. Il counter Q-03 confronta lo score
// con la media PRIMA che assorba lo score stesso. Motivazione al sito d'uso, ~:9660.
// ⚠️ NON e' esattamente byte-identico al vecchio (10, prevavg=0), ed e' giusto
// saperlo: l'algebra torna ma l'aritmetica INTERA no. `avg_new = (avg_prec+score)/2`
// tronca verso zero, e la direzione del troncamento dipende dal SEGNO della somma:
//     somma pari      -> ediff_vecchio = d/2 esatto        -> |d| <= 20, identico
//     somma dispari>0 -> ediff_vecchio = (d+1)/2
//     somma dispari<0 -> ediff_vecchio = (d-1)/2
// con d = score - avg_prec. La differenza e' confinata a |d| = 21 con somma dispari:
// una banda larga UN centipedone che fa scattare o no il contatore. Esempio verificato:
// avg_prec=-30, score=-9 (d=21) era "stabile" col vecchio, non lo e' col nuovo.
// 🔑 Il bench NON puo' vederlo: eval_stab_iters alimenta solo il fattore di tempo e
// il bench va a profondita' fissa. Resta 252074, ma non e' una verifica di QUESTO.
// PERCHE' comunque: il parametro smette di mentire di un fattore due. Finche' mentiva,
// qualunque SPSA su TMv2EvalWindow partiva da coordinate sbagliate — ed e' esattamente
// l'errore che il 12/08 ha fatto buttare un run intero, partito da init 5 invece di 20.
int g_tmv2_eval_prevavg = 1;
// Q-04 NodeTM bidirezionale: (base/100 - frac) * mult/100, clamp [min,max]/100
// (Alexandria 1.52/1.74 -> best che assorbe pochi nodi = alternative vive =
// fino a ~2.5x; best che domina = riduzione come il NodeTM classico).
int g_tmv2_node_base = 155;
int g_tmv2_node_mult = 130;
int g_tmv2_node_min = 36; // clamp inferiore %
int g_tmv2_node_max =
    263; // clamp superiore % (il ramo di ESTENSIONE che ci manca)
// Q-05 allocazione (parse_go, uci_mt.cpp): curva moves-left Caissa (mtg stimato
// cala col progredire della partita: anti overspend-apertura) + pool-increments
// Alexandria (l'inc entra nel montante diviso per mtg, NON come addendo fisso
// -> elimina alla radice la classe di bug F-012).
bool g_tmv2_alloc = true;
int g_tmv2_mtg_base = 35; // moves-left stimate a inizio partita
int g_tmv2_mtg_slope =
    67; // mtg -= slope*fullmove/100 (~35 a mossa 1 -> ~18 a mossa 40)
int g_tmv2_mtg_min = 14;
int g_tmv2_opt_pct = 124; // optimum = pool/mtg * questo/100
// Q-06 predicted-move cross-move (Caissa): a fine search salva l'hash della
// posizione ATTESA dopo pv[0]+pv[1]; al go successivo, avversario ha giocato la
// risposta predetta -> TT calda -> 0.915x, sorpresa -> 1.132x. Azzerato su
// ucinewgame.
bool g_tmv2_pred = true;
int g_tmv2_pred_hit = 945;   // per-mille
int g_tmv2_pred_miss = 1157; // per-mille
// TMPredBestThread: la predizione TM prende la PV dal thread CHE HA VINTO invece che
// sempre dal thread 0. A 1 thread e' identico (il vincitore e' sempre lo 0); a >1 thread
// riaccende una parte di TMv2 che oggi si spegne da sola ogni volta che vince un helper
// (536 volte su 5598 partite a 4 thread, misurato il 3/08/2026). Default OFF finche' non
// e' misurato: e' un cambio di comportamento nel regime multi-thread, che e' quello di
// CCRL (4 CPU nella 40/15 alta, 8 nella Blitz).
bool g_tm_pred_best_thread = false;
U64 g_tm_pred_hash = 0;
// Q-07 root-singularity stop (Caissa, search-based — NON il PolicyEasyMove
// bocciato): dopo timefrac% dell'optimum, a fine iterazione (depth>=9,
// |score|<scorecap), verifica dalla radice a depth/2 con la best ESCLUSA su
// finestra nulla a score - max(marginMin, marginBase - marginSlope*(depth-9)):
// se nessuna alternativa raggiunge la soglia la mossa e' "singolare" -> stop
// immediato.
static bool g_tmv2_rsing = true;
int g_tmv2_rs_margin_base = 405;
int g_tmv2_rs_margin_min = 225;
int g_tmv2_rs_margin_slope = 27;
int g_tmv2_rs_timefrac = 10;
int g_tmv2_rs_scorecap = 2627;
// Q-08: single-reply (unica mossa legale a root -> basta la prima iterazione
// completata, Caissa) + mate-stop (score di matto per N iterazioni consecutive
// -> stop, robusto ai matti instabili; SF ha PERSO partite CCC in attesa di
// questo fix).
static bool g_tmv2_single_reply = true;
static bool g_tmv2_mate_stop = true;
int g_tmv2_mate_iters = 7;

// ---- Gate per-TC del blocco TMv2 (2026-07-12, soglia base-time RIMOSSA 2026-07-24)
// ----------------------------------- Misura originale: TMv2 = -22.94 Elo
// @10+0.1 ma +23.81 @20+0.2 (segno opposto) -> nacque la soglia base-time
// (TMv2MinBaseMs=15000, scelta a mano, mai misurata). Sweep 24/07 su VM
// (screening 8s/12s/15s + conferma dedicata a piena concorrenza @8+0.08,
// nElo +31.10 +/-18.37, LOS 99.95%, LLR 54.3% verso [0,5]): NESSUNA
// regressione a nessun TC testato -> il vecchio cliff non si riproduce col
// codice attuale (probabilmente reso obsoleto dai bake successivi:
// ContHist/EPKeyFix/ecc.). Soglia base-time ELIMINATA; resta solo il gate
// sull'incremento (mai testato separatamente, prudenza invariata: TMv2 usa
// formule pool-increment che presuppongono un inc>0).
bool g_tmv2_tc_ok = true; // ricalcolato ogni parse_go (in uci_mt.cpp)
// TMv2IncGate (2026-08-02): se true (default storico) TMv2 e' spento a incremento zero.
// CCRL 40/15 e' 40 mosse in 15 minuti RIPETUTO, cioe' inc=0: il rating che ci misurano
// gira quindi sul TM v1 additivo, non sul TMv2 che vale +23,8 Elo. False = sempre attivo.
// BAKATO 2026-08-02 true -> false: TMv2 resta attivo anche a incremento zero.
// Misura: +27,55 ± 18,22 su 278 partite @40/40 inc=0 Hash 128 (regime CCRL), LLR 0,66
// in salita, intervallo [+9,3, +45,8] tutto sopra lo zero. SPRT non chiuso, ma il prior
// e' fortissimo: non e' una feature nuova, e' TMv2 (+23,8 Elo misurati nel regime con
// incremento) che questo gate spegneva in TUTTO il regime a inc=0, cioe' in CCRL 40/15.
bool g_tmv2_inc_gate = false;
void set_tmv2_inc_gate(bool v) { g_tmv2_inc_gate = v; }

// ---- Quarto audit, correttezza §D — BAKATI default-ON 2026-07-10
// ---------------- Giustificati su CORRETTEZZA (lettura + matetrack spot-check
// + non-regressione), NON su Elo: sono fix di edge-case che scattano
// rarissimamente, effetto Elo atteso ~0 e sotto la soglia di risoluzione dei
// nostri SPRT (vedi FOURTH_PASS_IMPL_LOG §"pattern negativo"). Bench di
// riferimento: 271715 (2026-07-21, solo S-11 tocca l'albero del bench). Q-29
// (Reckless fec9098): a root, non sovrascrivere un matto gia' provato da una
// iterazione completata con uno score piu' debole (TT collision / potatura).
static bool g_root_mate_restore = true;
// Q-30 (Reckless ae986df): r += 1 quando beta e' gia' decisivo (matefinding).
static bool g_lmr_decisive_beta = true;
// Q-32 (Stormphrax 3d27f98): alla probe TT, uno score decisivo la cui distanza
// di matto supera l'orizzonte regola-50 (100 - fifty plies) non e' realizzabile
// -> declassalo a sotto-decisivo (anti-GHI/shuffle). Complementare a TTCutFifty
// (che gata il CUTOFF, non la conversione dello score).
static bool g_tt_decisive_clamp = true;

// R-01 (quinto audit, SF 94beadff "important for elementary mate finding"): nel
// replacement scheme le entry con score DECISIVO (matto), flag non-EXACT e
// depth>=5 invecchiano piu' in fretta -> la TT non resta satura di score di
// matto stantii. Complementare a TTDecisiveClamp (quello declassa lo score
// letto, questo declassa il RANKING di rimpiazzo). Letto da tt_victim() in
// tt.h.
bool g_tt_secondary_age = false;

static inline int tt_clamp_decisive(int s, int fifty) {
  if (s > mate_score) {
    if (mate_value - s > 100 - fifty)
      return mate_score - 1;
  } else if (s < -mate_score) {
    if (mate_value + s > 100 - fifty)
      return -(mate_score - 1);
  }
  return s;
}
// S-11 (SF): stalemate-resource guard — non SEE-potare la cattura che sacrifica
// il nostro ULTIMO pezzo non-pedone quando siamo sotto (alpha<0). Fires raro.
static bool g_see_stalemate_guard = true;
// S-10 (SF): scrivi il risultato TB (WDL) in TT a depth+6 exact -> le rivisite
// colpiscono la TT invece di ri-probare fathom. NB: tb_score e' ply-embedded e
// sta sotto mate_score -> nessuna ply-normalization in TT (shuffle-risk lieve,
// accettato — vedi FOURTH_PASS_FINDINGS Q-32/S-10).
static bool g_tb_tt_store = true;
// Q-10 (Reckless 08f2cfa + SF d6483505, convergenza indipendente stesso mese):
// la verifica ProbCut gira a depth-1 in piu' quando improving (posizione in
// miglioramento = fail-high piu' credibile = conferma piu' economica basta).
static bool g_probcut_improving = true;
// Q-17 (Caissa): capture-history aggiornata anche ai beta-cutoff di QSEARCH.
static bool g_qs_capthist = true;
int g_qs_capthist_scale = 72; // % di td_stat_bonus(1)/malus — leva SPSA
                              // BAKED 6/08/2026 col vettore qsearch (vedi g_qs_delta)
                              // dedicata (bonus/malus qsearch fissi altrimenti)
// Q-16 (Caissa): extension=1 ai nodi PV se la mossa e' la ttMove ED e' una
// ricattura sulla casa dell'ultima mossa avversaria (gate strettissimo).
static bool g_recapture_ext =
    false; // Q-16 (Caissa): +2.05 Elo LOS 71.9% @2538g 15+0.15, ma LLR mai
           // concluso e raddoppia i nodi (bench 377398->780972) -> evidenza
           // troppo debole per il costo-albero, resta OFF. Riconsiderare dentro
           // il batch C con conferma piu' solida.
// Filtro di TENSIONE (2026-07-12): estende solo se lo scambio e' ~pari (|SEE|
// <= soglia), non ogni ricattura. Uno scambio nettamente sbilanciato (es.
// donna-per-pedone) e' gia' ovvio, l'estensione li' e' spreco puro di nodi; il
// caso utile e' quando la posizione dopo la sequenza resta davvero incerta.
// g_recapture_tension in centipawn, UCI RecaptureTension.
int g_recapture_tension = 70;
// Q-11 NodeCache (Caissa): bonus di ordering alle quiet vicino alla radice,
// proporzionale ai nodi che quella mossa e' costata nelle iterazioni/mosse
// precedenti. Vedi ThreadData::NCEntry (threads.h).
static bool g_node_cache = true;
int g_nc_bonus = 2080; // bonus max (quando la mossa ha assorbito TUTTI i nodi).
                       // Caissa: NodeCacheBonus 4096 [1000,8000]
int g_nc_min_sum =
    1066; // niente bonus finche' nodes_sum non supera questo (Caissa: 256)

// ---- Leve aggiunte 2026-07-10 per entrare nel MEGA co-tune (tutte 0 =
// comportamento
//      storico, bench bit-identico; il co-tune decide se valgono, niente SPRT
//      isolati) ----
// Q-18 (SF PR #6871, gainer): bonus della best move scalato sul numero di mosse
// cercate prima di lei. bonus *= (1024 + coef*moves_before) / 1024. 0 = piatto
// (off).
int g_hist_bonus_moves = 32; // coef, [0,512]
// Q-25 (Stormphrax 665b76b ri-tuna a 750/1024; Pawnocchio 3/4; Caissa 7/8): la
// history DECADE tra una mossa e l'altra invece di restare congelata. 1024 =
// nessun decadimento. Applicata solo a history_moves + capture_history (le
// conthist sono 2.4M entry: scansione troppo cara per il guadagno atteso; se il
// co-tune premia il decay, valutare anche quelle).
int g_hist_age = 940; // scala /1024, [512,1024]
// Q-26 (Caissa MoveOrderer.cpp:5-7, valori SPSA-tunati: quiet 802, cont 762,
// capt 346): history inizializzate a un PRIOR positivo invece che a zero
// ("innocente finche' provato colpevole" per le mosse mai viste). 0 = zero-init
// (storico). Co-tunare CON Q-25: fill e decay interagiscono (il decay tira
// verso 0, il prior sposta il punto di riposo).
int g_hist_init_quiet = 31; // [0,2000]
int g_hist_init_capt = 249; // [0,2000]
// Q-19 (Caissa Search.cpp:1897-1954): addenda al blocco LMRFine, in millesimi
// di ply. (a) ply-scaled PV: riduci MENO vicino alla radice — LMRFine non ha
// NESSUN termine di ply.
int g_lmrf_ply = 524; // [0,2048]  (Caissa: 1024)
// (b) killer/counter: Caissa le riduce ~2.6 ply in meno. Qui in millesimi,
// additivo al
//     `reduction--` intero di KillerLMRFix (che resta).
int g_lmrf_killer = 797; // [0,4000]

// Q-18 helper: bonus piatto -> scalato sul numero di mosse cercate prima della
// best.
static inline int td_hist_bonus_moves(int bonus, int moves_searched) {
  if (!g_hist_bonus_moves || moves_searched <= 1)
    return bonus;
  // moves_searched e' gia' incrementato: 1 = cutoff alla prima mossa (nessun
  // bonus extra).
  return (int)((long long)bonus *
               (1024 + (long long)g_hist_bonus_moves * (moves_searched - 1)) /
               1024);
}

// Aggressive LMR on/off (UCI "AggrLMR"). Default OFF. When ON, the history- and
// continuation-history-based LMR reductions use a SMALLER divisor and a WIDER
// clamp (g_aggr_lmr_div / g_aggr_lmr_clamp), so a very bad-history quiet can be
// reduced by several plies instead of just 1. Off reproduces the baseline
// exactly.
static bool g_aggr_lmr = false;
void set_aggr_lmr(bool v) { g_aggr_lmr = v; }

// ---- SPSA-tunable search parameters -----------------------------------------
// Exposed as UCI spin options (see set_search_param + uci_mt.cpp) so an
// external SPSA tuner (fastchess) can set them per-game without recompiling.
// Defaults = the current hand-set values. After a tuning run, bake the
// converged values in.
int g_rfp_margin = 53;  // reverse futility: static_eval - g*depth >= beta
                        // [SPSA-tuned: 30->21]
int g_razor_base = 272; // razoring: base + mult*depth below alpha -> qsearch
int g_razor_mult = 118;  // [SPSA-tuned: 102->139]
int g_fut_base = 181;   // futility: base + mult*depth (+improving bonus)
                        // [SPSA-tuned: 82->111]
int g_fut_mult = 138;    // [3.7 BAKE 53->41; BAKED #1 66->53]
int g_fut_depth =
    7; // gate profondita' del futility pruning (alzare = potare a nodi piu'
       // profondi = albero piu' stretto). UCI FutilityDepth, tunabile.
int g_fut_improving = 145; // extra futility margin when improving [SPSA-tuned:
                           // 60->93]
// Capture futility pruning (SF master Step 14, ported 2026-06-23). Default OFF
// (A/B). SF prunes a capture at low (reduced) depth when even winning the
// victim can't lift the static eval to alpha. Mirrors our qsearch
// capture-futility (best_score + value(victim) + margin) but in the main
// search. SPSA targets = the cp MARGINS (large range -> tunable);
// g_capfut_depth is a small-int GATE kept fixed (small ints round to noise
// under SPSA).
static bool g_cap_futility = true; // CaptureFutility (UCI spin 0/1)
int g_capfut_base = 156;           // REVERT 2026-07-23 (SPSA B1 evaporato @4452g)
int g_capfut_mult =
    203; // REVERT 2026-07-23 (SPSA B1 evaporato @4452g)
int g_capfut_chist =
    358; // REVERT 2026-07-23 (SPSA B1 evaporato @4452g)
         // [F-002 audit 2026-07-02: 125->123, SPSA+SPRT] capture-history
         // contribution, /1024 (SF=131; our capt-hist clamp +/-7000 -> max
         // ~896cp). Extremi testati: 60=-45 Elo, 140=-25 Elo (LOS 0.4%/2%,
         // entrambi conclusivi) -> ottimo STRETTO attorno al default; 123
         // neutro/lean positivo a 10+0.1 (+0.9) e 20+0.2 (+8.7), mai negativo
         // -> bakato.
int g_capfut_depth =
    8; // REVERT 2026-07-23 (SPSA B1 evaporato @4452g)
// PONTE DI SCALA cp -> unita'-eval per il termine vittima (BUG FIX 2026-07-16).
// see_piece_values[] (see.h) e' in cp CLASSICI (pedone=100); eval/alpha sono in
// unita'-eval, che girano a ~NORM_CP/100 = 3.92x la scala-100 e sono poi
// compresse da EvalScale (oggi 60) -> il fattore netto e'
// g_capfut_vic_scale*EvalScale/10000 = 2.35 a EvalScale=60. Senza ponte la
// vittima entrava a 1x = sotto-pesata ~2.4x -> `fut` troppo basso -> le catture
// BUONE venivano potate, l'esatto contrario di cio' che il commento al sito di
// uso dichiarava. Il gemello in qsearch (g_qfut_margin, ~riga 3360) ha da
// giugno un ponte *3 hardcoded e il suo commento documenta che ometterlo costo'
// -126 Elo: stesso bug, un sito fixato e questo no. Perche' segue EvalScale: *3
// fu tarato a EvalScale=56; a 60 il fattore giusto e' ~3.2. Legandolo a
// nn_get_eval_scale() il ponte non si ri-scalibra al prossimo bake di
// EvalScale. Default 392 = NORM_CP (la costante con cui threads.cpp:~5051
// converte score->cp mostrati): e' la stima onesta, non un numero inventato.
// (NORM_CP era 392 con la rete della 5.1; dal 10/09/2026 il DISPLAY usa 449 per
// legio-septima. Questo e' un parametro di RICERCA e non cambia con quello.) E'
// spin perche' l'SPSA lo assesti nel blocco CapFut*.
int g_capfut_vic_scale = 582; // CapFutVicScale, /10000 insieme a EvalScale
// --- Other missing SF-master cut features (ported 2026-06-23, ALL default
// OFF/legacy = byte-identical).
//     SPSA targets = the cp MARGINS (wide range); toggles + small-int gates are
//     NOT primary SPSA targets. Block to be co-tuned together at long TC on the
//     own-lineage net (lesson #13: never gate isolated). ---
static bool g_opp_worsening =
    true; // OppWorsening (SF :841): shave RFP margin when opponent worsening
int g_opp_worse_margin =
    23; // cp shaved from RFP margin when opp worsening (SPSA target)
static bool g_triple_ext =
    true; // TripleExt (SF :1244): +3 singular extension beyond the double
int g_singular_tmargin =
    319; // triple-ext margin below singular_beta (> dmargin) (SPSA target)
// Negative extensions as CONTINUOUS tunable amounts (reach off=0). Default
// (1,0) = legacy
// (-1 on ttMove>=beta, nothing on cutNode). SF = (3,2). SPSA-tunable
// (small-int, calibrated step).
int g_negext_tt = 2;  // NegExtTT (SF=3): -extension when ttMove fails high over
                      // beta; 0=off, 1=legacy
// CutNodeProp: propaga il tipo di nodo al primo figlio (`!is_cut_node` sui non-PV,
// come SF) invece di passare sempre `false`. Spin 0/1 e non `check`: il gestore
// generico fa `atoi`, quindi una `check` nuova non si accenderebbe mai.
int g_cutnode_prop = 0;
// LMRNegative: permette alla riduzione LMR di andare negativa (= estendere le mosse
// molto ben ordinate), come SF/Hobbes. Spin 0/1: il gestore generico fa atoi.
int g_lmr_negative = 0;
// PromoQS — MULTIVALORE (il gestore generico fa atoi, quindi spin, mai check).
//   0 = spento (comportamento storico)
//   1 = qsearch (tutte e 4 le promozioni) + MovePicker
//   2 = qsearch, SOLA DONNA                       <- niente MovePicker
//   3 = qsearch SOLA DONNA + MovePicker con guardia
//   4 = SOLO MovePicker con guardia (niente qsearch)   -> misurato NO-OP
//   5 = qsearch sola Donna, ESENTE dal contatore di QSMoveCap
//   6 = come 5, ma SOLO se la promozione non regala la Donna (SEE >= 0)  <- best
//
// 🔴 PERCHE' IL MODO 5. `td_score_move` da' a OGNI promozione SCORE_GOOD_CAPTURE +
// 50000 = 750.000, cioe' SOPRA TUTTE LE CATTURE. Con QSMoveCap=3 la qsearch cerca
// tre mosse per nodo: quindi in ogni posizione con un pedone in settima cercavamo
// **tre promozioni e ZERO catture**. Non stavamo aggiungendo una mossa, stavamo
// SOSTITUENDO le tre catture migliori. Il modo 5 esenta la promozione dal
// contatore, cosi' si aggiunge alle tre catture invece di sfrattarle. E' la stessa
// forma dell'esenzione-ricattura che manca nello stesso punto, e che quando manco'
// alla QFutility costo' -17 Elo.
//
// ⛔ MODO 4 FALSIFICATO: modi 2 e 3 danno nodi IDENTICI (1.834.041) su sei
// posizioni di promozione a depth 18. `skip_quiets` viene settato DENTRO il move
// loop, quando lo stage quieto e' gia' passato: non cancella mai una promozione.
// Il reperto (b) esisteva nel codice ma non nella pratica.
//
// 🔴 IL BISECT (bench, 4/08): 0=207259 · 1=242764 (+17,1%) · 2=241596 (+16,6%) ·
// 3=241596. Cioe': senza il picker il costo RESTA +16,6%, e la quota del picker e'
// 1.168 nodi = 0,5% della crescita. Anche togliere le tre sottopromozioni vale gli
// stessi 1.168 nodi. **Tutto il +17% sono le promozioni di Donna in qsearch**:
// promuovere crea una Donna nuova e la qsearch ne esplora tutte le catture e tutte
// le riprese, senza profondita' che la fermi. La mia diagnosi iniziale ("il picker
// e' la voce dominante") era SBAGLIATA, e il bisect l'ha falsificata in due minuti.
// Il modo 4 tiene la meta' che costa 0,5% e corregge comunque un difetto vero.
//
// 🔴 PERCHE' IL MODO 1 HA MISURATO -11,77 SU 502 PARTITE. L'albero cresce del 17%
// (bench 207259 -> 242764) e 55*log2(1,17) = -12,4 Elo di velocita' persa: il test
// ha misurato ESATTAMENTE il costo dei nodi in piu'. Non e' che le mosse non
// servano, e' che il modo di cercarle e' grossolano in due punti:
//  (a) generiamo TUTTE E QUATTRO le promozioni. Alfiere non serve mai, Torre quasi
//      mai: tre mosse su quattro sono zavorra, dentro un nodo che ne cerca 3 in
//      totale (QSMoveCap=3) -> spingono fuori le catture vere.
//  (b) nel MovePicker, prima skip_quiets saltava l'INTERA generazione; ora
//      generiamo tutta la lista quieta per buttarla via e tenere le promozioni.
//      Il costo non e' una mossa in piu': e' una movegen completa dove prima non
//      ce n'era nessuna. E' quasi certamente la voce dominante del 17%.
// 🔴 BAKED ON = 6 il 5/08/2026: +8,38 ± 6,67 su 2572 partite @25+0.25, LOS 99,31%,
// LLR 1,53. ⚠️ Questo cambia la FIRMA del motore: bench 207259 -> 225898.
int g_promo_qs = 6;
// TTCutExact (spin 0/1, default 0): esenta le entry EXACT dalla coerenza cutnode
// di TTCutRefine. Un'entry EXACT non ha direzione di bound.
int g_ttcut_exact = 0;
int g_negext_cut = 3; // NegExtCut (SF=2): -extension on a cutNode (ttMove not
                      // fail-high); 0=legacy/off
// NegExtOrder (spin 0/1, default 0 = ordine storico, byte-identico). Riordina la
// catena delle estensioni negative alla maniera di SF: cut node PRIMA del test su
// alpha. Con 0 il ramo NegExtCut e' IRRAGGIUNGIBILE (i due rami sopra partizionano
// tutto lo spazio in finestra nulla) — dettaglio al sito d'uso, ~:8184. 1 lo rende
// vivo. ⚠️ Finche' resta 0, NegExtCut va tenuto FUORI dallo spazio SPSA: tararlo
// significa misurare rumore, ed e' gia' successo (default 3 = massimo del range).
int g_negext_order = 0;
// ExclPruneGate (audit 8.0 P1, 24/09/2026; spin 0..2, default 0 = byte-identico).
// Dentro la ricerca singolare (excluded_move != 0) si cerca la stessa posizione senza la
// TT move, a finestra singular_beta-1/singular_beta. Una null move o una static eval che
// supera quella finestra la chiude subito: la TT move risulta "non singolare" senza che
// nessuna mossa vera lo abbia provato, e scattano multicut o estensioni negative al posto
// dell'estensione. Tutti gli 8 motori di riferimento escludono la NMP in quel caso;
// Stormphrax, Viridithas, Caissa, Integral e Reckless anche RFP/razoring.
// 1 = niente NMP nella ricerca singolare; 2 = niente anche RFP e razoring.
int g_excl_prune_gate = 0;
// FHBlend (audit 8.0 P2, 24/09/2026; check, default false = byte-identico).
// Su un fail-high non decisivo il valore restituito (e scritto in TT) viene tirato
// verso beta: best = (best*depth + beta)/(depth+1). SF, PlentyChess, Stormphrax,
// Viridithas e Caissa lo fanno a fine nodo; da noi esisteva solo su RFP e qsearch
// (FailHighSmooth). Un fail-high lontano da beta e' spesso gonfiato da potature a
// valle: un bound TT meno ottimista costa meno re-search ai padri. Il peso di beta cala
// con la profondita' (piu' ricerca = valore piu' fidato).
bool g_fh_blend = false;
// ---- Audit 8.0 gruppo 1, P3..P7 (24/09/2026). Tutti default OFF = byte-identico. ----
// P3 PrevRefuteMalus (SF update_all_stats "extra penalty for a quiet early move"): quando
// un nodo fa cutoff e la mossa QUIET del padre era la prima non-TT che il padre ha
// cercato, quella mossa e' stata confutata subito -> malus alla sua conthist (1 ply).
// Valore = percentuale di td_stat_bonus(depth); 0 = off. SF usa circa 70%.
int g_prev_refute_malus = 0;
// P4 ProbCutAdj (SF): il cutoff ProbCut ritorna pc_score - (probcut_beta - beta) invece
// del valore gonfiato di +margine (lo store TT resta a pc_score, come SF).
bool g_probcut_adj = false;
// P5 RFPNoTTPv (SF, Reckless): niente RFP sui nodi che la TT ricorda come PV.
bool g_rfp_no_ttpv = false;
// P6 PruneNPMGate (SF, Plenty): se il lato al tratto ha solo pedoni e re, niente LMP,
// futility, SEE e history pruning nel ciclo mosse (finali di pedoni: eval inaffidabile).
bool g_prune_npm_gate = false;
// P7 FutFailSoft (SF, Reckless): quando la futility quiet pota, il nodo fail-low
// restituisce almeno il valore di futility invece del best delle sole mosse cercate.
bool g_fut_fail_soft = false;
// ⛔ SingularNoPollute — PROVATO E SCARTATO il 12/08/2026, non rifarlo.
// L'idea: la ricerca singular rientra allo STESSO ply con `excluded_move` impostato, e
// sul suo beta-cutoff scrive `cutoff_cnt`, killer, counter-move e TUTTE le history —
// mentre due righe sopra `store_tt` e `td_corr_update` sono protetti da `excluded_move`.
// Sembrava una guardia che si ferma a meta'. Proteggere anche il resto:
//     bench 252074 -> 219752, cioe' -12,8% di albero a profondita' fissa (NON e' un
//     percorso raro: la frequenza si e' misurata cosi', senza sonde ne' contatori)
//     SPRT 15+0.15   3198 partite   -0,54 +/- 5,97   Ptnml [6, 375, 838, 378, 2]
//     SPRT 30+0.3    3012 partite   +1,61 +/- 6,04   Ptnml [2, 341, 807, 353, 3]
// Seimila partite su due TC, entrambe centrate sullo zero. ⚠️ A 1212 partite il test a
// 30+0.3 leggeva +14,63 +/- 9,53 con LOS 99,87%: leggere un SPRT a meta' strada qui
// avrebbe fatto bakare quindici Elo inesistenti.
// 🔑 PERCHE' non paga, ed e' la lezione: l'asimmetria e' COERENTE, non distratta.
// `store_tt` e `td_corr_update` registrano informazione sulla POSIZIONE, che escludendo
// la mossa migliore sarebbe falsa; le history registrano informazione sulla MOSSA, che
// resta vera anche li'. E il -12,8% lo conferma: senza pollution l'albero e' piu' piccolo,
// quindi a tempo fisso il motore va PIU' A FONDO — e pareggia lo stesso. Quei nodi in piu'
// non erano sprecati, compravano ordinamento. Il dato vale quanto costa raccoglierlo.
// CapturedMailbox (spin 0/1, BAKED A 1 il 9/08/2026; 0 = comportamento storico).
// P2/B3 dell'audit del 5/08. `td_captured_piece` scorre SEI bitboard per scoprire
// quale pezzo sta su una casa; la mailbox `td.piece_on[64]` — gia' mantenuta in
// make/unmake e inizializzata dalla radice — da' la stessa risposta con un lookup.
// 🔑 NODE-IDENTICAL per costruzione: cambia COME si ottiene il valore, non il
// valore. Il bench DEVE restare identico, ed e' anche il controllo che dice se
// l'implementazione e' sbagliata.
// Il fallback conta: la versione storica ritorna `P` quando sulla casa non c'e' un
// pezzo AVVERSARIO (casa vuota -> mailbox -1, oppure pezzo amico -> fuori banda).
// La condizione `pc >= start && pc <= end` al sito d'uso riproduce quel caso.
// (Pulizia 25/09/2026: tolta l'opzione di compilazione TRIUMV_NO_MAILBOX, la mailbox e'
// sempre mantenuta. Il vecchio percorso a loop e' in _backup/Triumviratus_8.0_src_2026-09-25_pre_cleanup.)
// MISURA (9/08/2026, laptop Zen4, un solo binario Trium_p2_avx2, 160 posizioni UHO
// interlacciate a depth 20): mediana B/A da +1,01% a +0,73%, mai sotto lo zero in
// nessun blocco da 10. L'audit ne stimava +0,2-0,7% per il LOTTO P2 intero: qui
// il solo B3 rende di piu'. Resta velocita' pura, ~0,5-1 Elo.
// Toggle a RUNTIME e non #ifdef proprio per questo: fra due build PGO diverse il
// pavimento di rumore e' ~0,5% e un effetto dell'1% non sarebbe separabile; con un
// solo eseguibile e due setoption la varianza di layout si cancella per costruzione.
int g_captured_mailbox = 1;
static bool g_corrval_margin =
    true; // CorrValMargin (SF :980): fold |corr| into RFP margin (prune less
          // when eval heavily corrected)
int g_corrval_rfp = 41; // |corr|*g/128 added to RFP margin (SPSA target)
// CorrValExt (5.0-B, audit SF-master/Reckless 2026-06-24, default OFF): estende
// il fold di |corr| OLTRE l'RFP -> anche futility, SEE, LMR (eval pesantemente
// corretta = incerta -> pota/riduci di MENO). Pesi co-tunabili. OFF =
// byte-identico.
static bool g_corrval_ext = true;
int g_corrval_fut = 38; // futility: margine += |corr|*g/128 (prune less)
int g_corrval_see = 27; // SEE: soglia -= |corr|*g/128 (prune less)
int g_corrval_lmr = 83; // LMR: reduction -= |corr|*g/4096 ply (reduce less)
// ⭐ P1a CorrUncert (2026-07-14, IDEA ORIGINALE, default OFF): il DISACCORDO tra
// le tavole di correzione (pawn vs minor vs major) come segnale di INCERTEZZA
// locale della static eval. Distinto da CorrValMargin/CorrValExt (che usano
// |corr| = bias sistematico NOTO e stabile): qui misuriamo quanto le tavole
// LITIGANO tra loro = il residuo search-eval e' instabile/contestuale in questa
// regione = la static eval e' inaffidabile QUI -> margini di pruning piu'
// larghi (RFP/futility potano di meno). Bassa discordia = eval affidabile =
// pruning pieno. Nessun motore noto usa varianza/disaccordo delle correction
// history.
static bool g_corr_uncert = true;
int g_cu_rfp = 60; // RFP:      margine += uncert * g/64
int g_cu_fut = 71; // futility: margine += uncert * g/64
int g_cu_cap = 70; // cap (cp) del segnale di disaccordo

// ⭐ P4 TroubleMaking (2026-07-14, IDEA ORIGINALE, default OFF): in posizione
// PERSA (score < -Score), se un'altra mossa di root e' costata all'avversario
// uno sforzo COMPARABILE alla best (NodeCache root: nodi >= best*Effort% =
// difficile da refutare) e una verifica null-window a depth/2 conferma che vale
// quasi quanto la best (>= score-Margin), gioca QUELLA: massimizza le chances
// pratiche ("complica la vita") a costo teorico limitato e verificato.
static bool g_trouble = false;
int g_trouble_score = 150; // "persa" = score < -N (unita' search)
int g_trouble_effort = 60; // candidato se nodi >= best_nodes * N%
int g_trouble_margin = 40; // verifica: cand >= score - N

// BrilliantSac (6.0, default OFF): estende (+1) le catture PERDENTI di materiale
// (SEE < 0) ma con capture_history altissima -> sacrifici strutturali che il
// motore ha imparato essere buoni, da proteggere dalle riduzioni LMR pesanti.
// v1 FIX 2026-07-23: il default 10000 non scattava MAI (supera HISTORY_MAX=7000).
// STORIA (2026-07-23): v1 (soglia FISSA 5000) = +2.70+-7.25 LOS 77% @2190g, lean
// troppo debole per chiudere. v2 (soglia SCALATA sull'entita' del sac + !in_check)
// = -17.83 LOS 1.3% @390g, NEGATIVO NETTO (la scalatura estendeva su rumore).
// Ripristinata la v1 come RESTING STATE (default OFF) e candidato co-tune:
// `BrilliantSacMargin` gia' SPSA-ready. Non bakare standalone (+25% albero per un
// lean non confermato); lo SPSA decidera' margine + coordinamento coi margini vicini.
// STORIA SPSA B1_capture_brilliant (2026-07-23, VM trium-eu-96, mirror 16+0.16):
// bakato IN ANTICIPO su due checkpoint concordi (theta1 iter780 +5.82+-12.75 LOS81%;
// theta2 iter1090 +5.95+-10.35 LOS87%, LLR in salita). RICONFERMA a piu' partite:
// a 4452 games l'effetto e' EVAPORATO (+1.64+-5.18, LOS73%, LLR piatto 4.6%) ->
// classico regression-to-mean/winner's-curse. REVERT completo (2026-07-23) a tutti
// i default pre-bake. L'intero blocco (11 param + BrilliantSac) resta candidato
// SOLO per un mega co-tune con kill-switch a metà-run rispettato fino in fondo
// (non un bake anticipato su un trend a 2 checkpoint), non per un retest isolato.
static bool g_brilliant_sac = false;
int g_brilliant_sac_margin = 5000; // caphist >= N (max utile = HISTORY_MAX 7000)
// BrilliantSac-B (reduction-cap, 2026-07-23): invece di ESTENDERE (v1, che tocca
// solo la 1a mossa e gonfia l'albero +25%), protegge il sac strutturale dalla
// RIDUZIONE LMR (reduce-less). Complementare a v1: agisce sui sac ordinati TARDI
// (il caso comune) a costo albero molto minore -> piu' fedele all'intento
// "proteggere dalle riduzioni pesanti". Stesso trigger di v1 (is_brilliant).
static bool g_brilliant_sac_lmr = false;
int g_brilliant_sac_lmr_amt = 2; // plies di riduzione LMR risparmiati sul sac

// MalusScaled (5.0-B, default OFF): il malus history alle quiet
// provate-e-fallite scala col loro move-order (le tardive ricevono malus
// MINORE, come Reckless). OFF = malus piatto = byte-identico.
static bool g_malus_scaled = true;
int g_malus_scale_coef =
    59; // denom = 1024 + coef*indice -> malus = bonus*1024/denom
// ⭐ MalusQuad (2026-07-18, scan Reckless 5e669a6 "Apply decay to quiet move
// malus", STC +4.67 / LTC +5.18): stessa idea del decay che abbiamo gia', ma la
// caduta col move-order e' QUADRATICA invece che lineare -> le quiet molto
// tardive prendono un malus quasi nullo. E' l'unica patch dello scan che sia un
// upgrade diretto di codice nostro gia' presente e gia' positivo. Reckless usa
// coef 45 per il quadratico (il nostro 59 e' tunato per il LINEARE, non
// riusarlo). OFF = decay lineare attuale = byte-identico.
static bool g_malus_quad = false;
int g_malus_quad_coef =
    45; // denom = 1024 + coef*indice -> malus = bonus*(1024/denom)^2
// ⭐ Guard di razoring (scan Reckless: la famiglia "guard di pruning" aggrega
// ~+35 Elo LTC). Il razoring butta il nodo in qsearch quando l'eval e' molto
// sotto alpha. Ma la qsearch vede SOLO catture: se la posizione non e'
// tatticamente ovvia, quel giudizio non vale niente.
//   [1] RazorTTQuiet (2b0812d, STC +3.64 / LTC +2.49): razora SOLO se la TT
//   move e' rumorosa
//       (cattura/promozione). TT move quiet -> la posizione non e' tattica ->
//       NON razorare.
//       ⚠️ Fedele a Reckless: NESSUNA tt move = niente razoring (per loro
//       Move::NULL e' "quiet"). Il nostro razor e' gia' VERIFICATO (giriamo la
//       qsearch e torniamo solo se conferma), quindi e' gia' piu' sicuro del
//       loro: il guard potrebbe rendere meno. Da misurare.
//   [2] RazorTTLower (b2ad933, STC +1.10 / LTC +1.85): in piu', non razorare se
//   il bound in TT
//       e' LOWER (hash_flag_beta = ha gia' fallito alto qui) -> l'eval bassa e'
//       sospetta.
// Entrambi OFF = byte-identico.
static bool g_razor_ttquiet = false;
static bool g_razor_ttlower = false;
// ⭐ SingularExactMargin (3461653, STC +2.58 / LTC +4.94; con 00a31a7 la
// famiglia fa ~+8 LTC): il margine singular oggi e' uguale per tutti i nodi.
// Reckless lo DIMEZZA quando il bound in TT e' EXACT: score esatto = piu'
// affidabile = si puo' essere piu' generosi nel dichiarare singolare una mossa
// (singular_beta piu' alto -> piu' estensioni proprio dove il valore e'
// fidato). ⛔ SBAKATA 2026-07-19 dopo essere stata bakata il 18/07. STORIA
// COMPLETA, da non ripetere:
//    +8.60 +/- 7.62 (LOS 98.65%, 2304g @10+0.1)  ->  -8.60 +/- 10.54 (LOS 5.5%,
//    1010g @20+0.2) Le due misure NON erano in disaccordo: sono la stessa patch
//    sui DUE LATI DI UN CROSSOVER. Bench a profondita' variabile (l'unico
//    strumento che l'ha spiegato):
//        depth 13: -7.4%   depth 17: -11.1%   depth 19: +15.6%   depth 20:
//        +42.7%
//    MECCANISMO: il margine e' `mpd*depth` con mpd=1, quindi dimezzarlo toglie
//    depth/2 e l'aggressivita' SCALA con la profondita'. Peggio: le soglie di
//    doppia/tripla estensione sono misurate DA singular_beta, quindi alzandolo
//    diventa piu' facile anche il doppio e il triplo
//    -> cascata che esplode in profondita'.
//    TENTATIVI DI SALVATAGGIO, entrambi FALLITI in partita:
//      * SingularExactDecouple (doppia/tripla ancorate al margine pieno): d20
//      da +42.7% a +8.5%
//      * SingularExactMaxDepth=18 (cap): d20 a +0.006%, meccanicamente
//      PERFETTO...
//        ...ma SPRT CAP18 vs OFF @20+0.2 = -2.07 +/- 8.79, LOS 32%, 1508g ->
//        nemmeno il cap paga.
//    Conclusione: il beneficio a bassa profondita' non sopravvive quando la
//    ricerca passa il grosso del tempo sopra il crossover. Codice e toggle
//    TENUTI come documentazione della misura. LEZIONE: benchare a piu'
//    profondita' PRIMA di bakare qualsiasi patch di estensioni/riduzioni.
static bool g_singular_exact_margin = false;
// Vedi il commento al sito d'uso: ancora doppia/tripla estensione al margine
// PIENO.
static bool g_singular_exact_decouple = false;
int g_singular_exact_maxdepth =
    0; // 0 = nessun cap. Vedi il sito d'uso: sopra ~18 il dimezzamento esplode.
// DoDeeper/DoShallower (5.0-B, audit SF-master, default OFF): dopo che la
// re-search LMR a piena profondita' fallisce alto, aggiusta la profondita' in
// base a QUANTO batte il best corrente (molto sopra -> +1 ply; appena sopra ->
// -1 ply). OFF = full_depth invariato = byte-identico.
static bool g_do_deeper = false;
int g_dodeeper_base =
    43; // do_deeper se score > best + base + 2*full_depth (SPSA target)
int g_doshallower_margin =
    7; // do_shallower se score < best + margin (SPSA target)
// 5.1 HindsightExt (Pawnocchio): se un nodo non-PV e' stato RIDOTTO di >=
// margine plies e l'eval e' girata male (static_eval + prev_eval < 0),
// ri-estendi (+1): la riduzione era un errore "col senno di poi". Default OFF =
// byte-identico (reduction_stack mai letto).
static bool g_hindsight_ext =
    true; // 5.1 BAKE ON (isolata +12.7 LOS 97.6%% @682g vs 5.1; +61 LOS 100%%
          // vs 5.0). Pawnocchio-style.
int g_hindsight_margin =
    3; // [F-006 audit 2026-07-02: 3->4, SPRT +5.96 Elo LOS75% @408g] ply minimi
       // di riduzione perche' scatti l'hindsight (soglia piu' selettiva = meno
       // ri-estensioni sprecate)
// 5.1 HindsightRed (Stormphrax #300, complemento dell'extend): se un nodo
// non-PV e' stato RIDOTTO
// >= margine plies e l'eval e' girata BENE (static_eval + prev_eval >= soglia),
// RIDUCI (-1): la posizione regge nonostante la riduzione -> risparmia nodi.
// Default OFF (in test). Soglia in scala EvalScale-compressa (Stormphrax usa
// 202 in scala piena ~= 113 nostra).
static bool g_hindsight_red = false;
int g_hindsight_red_margin = 2; // ply minimi di riduzione (Stormphrax usa 2)
int g_hindsight_red_thresh =
    113; // static_eval + prev_eval >= soglia (co-tunabile)
// BadNoisy (5.0-B, audit SF-master qsearch move-count pruning, default OFF): a
// nodo qsearch ordinato best-first, dopo N catture provate le rimanenti sono
// quasi-sempre perdenti -> skip. Niente ricattura/promo. OFF = nessuno skip =
// byte-identico.
static bool g_bad_noisy = true;
int g_bad_noisy_count = 7; // REVERT 2026-07-23 (SPSA B1 evaporato @4452g)
                           // (SPSA target; 3=SF/aggr.)
// LMREnrich (archivio 4.2, audit SF, default OFF): se la TT-move è NOISY
// (cattura/ep/promo) mentre la mossa corrente è quiet (qui sempre, blocco
// is_quiet), le quiet sono meno probabili -> riduci di PIU' (SF: ttCapture &&
// !capture -> r += ...). Su b1a57 era −25 (over-reduction); ri-test sul net
// own-lineage.
static bool g_lmr_enrich = true;
int g_lmr_enrich_amount =
    2; // ply extra di riduzione quando la TT-move è noisy (SPSA target)
int g_razor_quad_coef = 82; // RazorQuadCoef (SF :967): quadratic razor term
                            // (base + mult*d + coef*d^2); 0 = linear/off
int g_rfp_depth_cap = 7; // RFPDepth: >0 overrides RFP depth cap (0=legacy 6/8);
                         // widen toward SF=17
int g_razor_depth_cap = 6; // RazorDepth: >0 overrides razor depth cap (0=legacy
                           // 3/4); widen toward SF (uncapped)
// IID — Internal Iterative DEEPENING (5.1, default OFF). L'IIR economico
// (depth-- senza TT move, linea ~2803) NASCONDE il problema dell'ordinamento
// scadente; l'IID lo RISOLVE: senza TT move fa una mini-ricerca ridotta PRIMA
// per OTTENERE una hash move (ordinamento ~SF), cosi' il taglio "morde"
// (chicken-egg col cut-block). Solo su pv/cut node, con freno anti-cascata (no
// IID dentro IID). OFF = byte-identico (blocco saltato). Costa +nodi (la
// mini-ricerca) ma ordina molto meglio.
static bool g_iid = false;
int g_iid_depth = 4; // IIDDepth: profondita' minima per attivare l'IID
int g_iid_reduction =
    2; // IIDReduction: ply tolti alla mini-ricerca (iid_depth = depth - r)
// Q-15 FollowPV (SF PR #6656): si salva la PV dell'iterazione precedente; i
// nodi che la RISEGUONO (stessa linea di mosse dalla root) NON fanno IIR ne'
// shallow-pruning (futility/LMP): sulla linea principale l'ordinamento e' gia'
// buono e potare li' rischia di scartare la mossa migliore. Flag propagato
// lungo la sola PV-line. 0 = OFF (byte-identico). Usa td.prev_pv[] (salvata a
// fine iterazione).
static bool g_follow_pv = false;
// ---- F-002/F-004/F-005 (audit 2026-07-02): costanti hardcoded promosse a
// tunable + LMR-catture. TUTTI i default = valore hardcoded storico =
// comportamento BIT-IDENTICO (verificato via bench); si accendono solo dal
// tuning/SPRT.
// 🔴 BAKED il 6/08/2026: 3000 -> 1525. Il 3000 era il MASSIMO del range (200..3000),
// cioe' il delta pruning in quiescence era di fatto SPENTO, e ci era arrivato spinto da
// due SPSA precedenti. Non era una taratura: era un meccanismo disattivato per sbaglio.
// Co-tunato con QSCaptHistScale via SPSA a 20+0.2 (830 iter), gate a 30+0.3:
//   +7,37 +/- 5,65 su 4.480 partite, LLR 2,00 (67,8%).
// ⚠️ Il vettore COSTA nodi: bench 251.855 -> 261.287 (+3,74%), ~2,9 Elo di debito a
// 55 Elo/raddoppio, quindi il guadagno lordo del meccanismo e' piu' alto del misurato.
// ⚠️ Il bisect fra le due leve NON e' stato fatto: non sappiamo quale delle due paghi.
int g_qs_delta = 1525; // F-002: delta-pruning qsearch (era const DELTA=1000; a
                       // scala-56 ~1785cp reali)
int g_singular_mpd =
    1; // F-002: margine singular PER-DEPTH (era hardcoded 2*depth, unita'-eval)
int g_tm_drop_thresh = 6; // F-002: soglia score-drop TM (era hardcoded 8,
                          // unita'-eval) [BAKE 2026-07-03 TM post-F-003: 8->6]
int g_tm_drop_cap =
    160; // F-002: cap score-drop TM (era hardcoded 200, unita'-eval) [BAKE
         // 2026-07-03 TM post-F-003: 200->160]
int g_nodetm_base = 133; // F-002: NodeTM scale = (base - frac*100)/100
                         // (era 1.4) [BAKE 2026-07-03 TM post-F-003: 140->133]
int g_nodetm_min = 58;   // F-002: NodeTM clamp inferiore % (era 0.6) [BAKE
                         // 2026-07-03 TM post-F-003: 60->58]
// F-008 (audit 2026-07-02): SEE a soglia con early-exit (see.cpp: td_see_ge).
// SeeGE OFF (default) = td_see classico, byte-identico. SeeGEVerify = oracle:
// esegue ENTRAMBI i path a ogni chiamata e segnala i mismatch (0 attesi).
bool g_see_ge = true; // [F-008 2026-07-02: ON] +1.81% NPS su test rigoroso (150
                      // pos x2, depth 15, nodi BIT-IDENTICI 73589060=73589060
                      // -> solo velocita', zero rischio search)
bool g_see_ge_verify = false;
static std::atomic<long long> g_seege_checks{0}, g_seege_bad{0};
static void seege_verify_report() {
  if (g_seege_checks > 0) {
    printf("info string SeeGEVerify: %lld checks, %lld mismatch\n",
           (long long)g_seege_checks, (long long)g_seege_bad);
    fflush(stdout);
  }
}
int g_lmr_min_depth =
    3; // F-005: depth minima per la LMR (SPSA: stabile a 3, confermato)
int g_lmr_min_moves = 1; // [F-005 BAKE 2026-07-02: 3->2, SPSA+SPRT in blocco
                         // LMR] riduce dalla 3a mossa (SF dalla 2a)
static bool g_lmr_captures =
    true; // [F-004 BAKE 2026-07-02] LMR anche sulle CATTURE
          // (SF/Stormphrax-style). Vettore SPSA (MinMoves=2, EvalMargin=20)
          // SPRT: +7.38 LOS 87% @800g 8+0.08
// LMRCheckExempt (2026-07-26) — default 0 = byte-identico.
// Entrambi i rami LMR (quiet :7490 e catture :7741) sono gated da
// `!in_check && !gives_check`: **nessuna mossa che da' scacco viene MAI ridotta,
// e in un nodo in scacco nessuna mossa viene ridotta.** SF non ha nessuno dei
// due gate: la sua LMR (search.cpp:1327) e' condizionata solo da
// `depth >= 2 && moveCount > 1`.
// MOVENTE: e' la TERZA esenzione-scacchi del motore, e le prime due, tolte, hanno
// pagato — rimozione della check-extension **+7.98 Elo** (19/07) e rimozione degli
// scacchi quieti in qsearch **+9.71 Elo** (25/07, bound chiuso @20+0.2). Il motore
// era sistematicamente troppo generoso con gli scacchi in tre posti distinti.
//   0 = comportamento attuale (entrambe le esenzioni)
//   1 = si riducono anche le mosse che DANNO scacco  <- testare QUESTA per prima
//       (rischio piu' basso: tocca solo le mosse figlie, non il regime del nodo)
//   2 = si riduce anche nei nodi IN scacco (tattico, molto piu' rischioso)
// ⚠️ Tocca OGNI nodo: bench a profondita' 13/17/20 sul LIBRO dei gate PRIMA di
// qualunque SPRT — e' la regola nata da SingularExactMargin, che stringeva
// l'albero a d13/d17 e lo faceva ESPLODERE (+42.7%) a d20.
int g_lmr_check_exempt = 0;
int g_lmr_cap_scale = 52; // REVERT 2026-07-23 (SPSA B1 evaporato @4452g). F-004: % della lmr_table applicata alle catture (50
                          // = dimezzata; SPSA confermo' ~50)
int g_singular_dmargin = 59; // double-extension margin below singular_beta
                             // [SPSA-tuned: 63->43]
int g_hist_red_div = 3190;   // LMR history-reduction divisor [SPSA-tuned:
                             // 3500->1041]
int g_asp_init_delta = 19; // aspiration: initial window half-width [SPSA-tuned:
                           // 25->31]
// ⭐ AspStableShrink (2026-07-18, scan Reckless 4192617 "Shrink initial
// aspiration window if our search has been stable", STC +4.58 / LTC +2.08). Se
// la ricerca e' stata stabile (stessa best move E score fermo per N iterazioni
// consecutive), la finestra iniziale puo' partire piu' STRETTA: una finestra
// stretta che non fallisce = ricerca piu' economica; se poi fallisce si
// riallarga comunque. Loro: delta 23 - min(eval_stab, pv_stab, 7). Noi delta 19
// -> cap 4 (= il cap del nostro eval_stab_iters) e' la scala equivalente.
// ⚠️ Vive nel ramo TM (thread 0 + timeset) -> a profondita' fissa NON
// scatta: il BENCH NON CAMBIA, non e' un errore. La verifica byte-identica va
// fatta per costruzione, non col bench.
static bool g_asp_stable_shrink = false;
int g_asp_stable_cap =
    4;               // massimo restringimento del delta iniziale (AspStableCap)
int g_asp_grow = 34; // aspiration: growth % on fail [SPSA-tuned: 100->31]
// Q-28 AspScoreMult (Stormphrax 9081510+03d4c83): la finestra iniziale si
// ALLARGA col quadrato dello score (EMA). avgSq = (avgSq + score*|score|)/2
// aggiornato per iterazione; delta += (i64)|avgSq| * mult >> 20. Score grandi
// (verso il matto) -> finestra piu' larga -> meno ri-cerche. 0 = OFF (nessun
// termine, byte-identico). ⚠️ i64 obbligatorio: score*|score| ~9e8, *mult
// sfora i32 (overflow che Stormphrax ha dovuto fixare con un commit dedicato).
// NON accoppiare con AspAvg (revertito).
int g_asp_score_mult =
    38; // >> 20 scale; SPSA range [0,2000], init-when-ON ~100
int g_cmhc_scale =
    6; // Q-12 CMHC: bonus conthist *= (100 + scale*consistenza)/100. 0 = neutro
// CMHCPly1 (spin 0/1, default 0 = comportamento attuale, byte-identico).
// 🔴 DIFETTO trovato il 7/08/2026. Dei cinque lookup di `td_cmhc_factor` quattro
// sono corretti e uno no — proprio il termine 1-ply, che e' l'unico SEMPRE attivo.
// La convenzione del move stack e' documentata a :6618-6620 e imposta dalla
// scrittura: `move_stack[td.ply]` e' la mossa che porta IN questo nodo (1 ply
// indietro), `move_stack[td.ply-1]` sono 2 ply indietro.
//   SCRITTURA (:8675-8677, :8714):  continuation_history[ move_stack[ply] ][mossa]
//   LETTURA   (:6869, oggi):        continuation_history[ move_stack[ply-1] ][mossa]
// Non e' "un contesto diverso": e' una cella che nessuno ha mai scritto con quel
// significato, quindi il bit di consistenza che ne esce e' rumore. Con scale=6 e
// consistent in [0,5], un contributo su cinque casuale = ±6% di rumore sul bonus
// conthist a ogni beta-cutoff quiet.
// 🔴 MISURATO il 7/08/2026 e lasciato a 0 — NON perche' il fix sia sbagliato.
//   in pacchetto con Rule50Formula, 20+0.2 : -20,33 +/- 16,74 su 462 partite
//   da solo, 40+0.4                        : negativo anche li'
// Due TC opposti concordi ⇒ non e' una questione di regime.
// 🔑 Il fix RESTA CORRETTO: scrittura (:8675/:8714) e lettura ora usano la stessa
// chiave, mentre prima la lettura interrogava una cella che nessuno aveva mai
// scritto con quel significato. Cio' che perde non e' la correttezza — e' che
// `CMHCScale=6` era stato tarato ATTORNO al lookup rotto. Col bug il termine 1-ply
// era un bit ~casuale (positivo circa meta' delle volte); corretto diventa il piu'
// informativo dei cinque e sara' positivo molto piu' spesso, quindi il fattore sale
// sistematicamente e i bonus conthist si gonfiano oltre quanto la taratura
// prevedesse. Il difetto era stato ASSORBITO dalla taratura fatta sopra di esso.
// ⇒ SEGUITO: serviva un SPSA su `CMHCScale` e sui parametri conthist che ci stanno
//   sopra, con CMHCPly1=1 FISSO. FATTO l'8/08/2026 — e NON ha sbloccato niente.
//
// 🔴 ESITO DELLA RITARATURA (8/08/2026). SPSA a 13 parametri, 2141 iterazioni,
// TC 20+0.2, Hash 64. Il vettore trovato abbassa il peso della conthist ovunque,
// che e' la direzione prevista dalla teoria: CMHCScale 6 -> 1,5 (un quarto),
// ContHistWeight -33%, ContHistDiv +79%, LMRContHistDiv +132%. Ma ai gate non
// produce niente, in NESSUNO dei tre regimi provati:
//     pacchetto (fix+vettore)  Hash 128 @30+0.3 :  -1,63 +/-  9,98  su 1064
//     soli parametri, no fix   Hash 128 @30+0.3 :  -8,36 +/- 14,41  su  582
//     pacchetto                Hash 256 @30+0.3 : -10,24 +/- 10,89  su 1018
// ⚠️ Il tuning e' stato fatto a 20+0.2/Hash 64 e gattato a 30+0.3/Hash 128-256:
//   la differenza di regime e' una spiegazione plausibile del fallimento, e non
//   e' stata separata (il log.csv del run e' andato perso con la VM, quindi non
//   si e' potuto nemmeno verificare la convergenza ne' le derive per parametro).
// ⇒ STATO: CMHCPly1 resta 0. Il fix e' corretto e non paga. Per riaprirlo servono
//   un tuning fatto DIRETTAMENTE a 30+0.3, con meno parametri e il log conservato.
//   Non e' una priorita': tre gate su tre dicono che li' dentro non c'e' Elo.
// 🔑 Lezione generale, valida oltre questo caso: un difetto che vive nel codice
//   abbastanza a lungo viene assorbito dalle tarature che gli si costruiscono
//   sopra, e correggerlo ISOLATAMENTE puo' peggiorare il motore. Non vale per i
//   difetti che non hanno parametri tarati sopra — es. CorrTBGuard.
int g_cmhc_ply1 = 0;
int g_probcut_margin =
    234; // ProbCut: capture verification must beat beta by this margin
int g_probcut_improve = 4; // Q-20b (Alexandria): abbassa probcut_beta di questo
                           // quando improving (probcut piu' facile). 0 = OFF

// ⭐ GATE STRUTTURALI (2026-07-17): interi hardcoded delle DECISIONI DI
// PROFONDITA' — mai tunati perche' troppo piccoli per SPSA (range<=4 =
// perturbazione annullata), NON perche' ottimi. Esposti qui coi valori attuali
// (byte-identico) per test DISCRETO via SPRT. SF/Berserk li tunano. SCREENING
// 2026-07-17 (12+0.12): l'IIR e' GIA' OTTIMO, tutte e 3 le varianti NEGATIVE.
// Chiuso.
//   IIRNotInCheck=true  -> -5.79  (LOS 3.7%)   il "fix" annulla-check-ext e'
//   negativo a STC IIRMinDepth=6       -> -10.08 (LOS 0.1%)   sparare MENO
//   (gate 4->6) fa male IIRMinDepth=3       -> -14.67 (LOS 0.5%)   sparare PIU'
//   (gate 4->3) fa peggio -> 4 e' il punto giusto
int g_iir_min_depth =
    4; // IIR: depth minima perche' scatti il depth-- senza TT move (:3894)
int g_iir_amount = 1; // IIR: di quanto riduce
static bool g_iir_not_in_check =
    false; // fix bug: IIR NON spara in scacco. Testato -5.79 -> resta OFF
// ⭐ SCREENING 2026-07-17 (12+0.12): gate 5->7 = +12.54 +/-9.52 (LOS 99.5%,
// 1330g) = PRIMO HIT VERO.
//   ProbCut deve sparare MENO (a depth 5-6 la verifica ridotta e'
//   inaffidabile). 5->4 era neutro (-0.46). mind9 = +4.42 (LOS 72%) < mind7 ->
//   picco ~7.
// ⚠️ RICONFERMA 20+0.2 (2026-07-18): il +12.5 e' EVAPORATO. PC7-solo =
// +0.67 (LOS 55%), INCR(PC7+SG4)
//   = +3 sfumando (LOS 76%). Puro TC-decay: a 20s la ricerca profonda rende
//   ProbCut gia' affidabile.
//   -> gate=5 RESTA. L'hit STC vale solo a blitz. Search-side confermato tunato
//   al TC di release.
int g_probcut_min_depth = 5; // ProbCut: depth minima del gate (:4157)
// SCREENING 2026-07-17 (12+0.12): off=4 e' gia' ottimo. off=3 (verifica piu'
// profonda) -1.57 neutro;
//   off=5 (verifica piu' bassa) -8.92 (LOS 5%) fa male. Chiuso: pc_depth =
//   depth-4 sta bene.
int g_probcut_depth_off = 4; // ProbCut: pc_depth = depth - questo (:4205)
// SCREENING 2026-07-17 (12+0.12): ttm=2 (piu' esigente, meno singular) -8.03
// (LOS 7%) male;
//   ttm=4 (meno esigente, PIU' singular) +6.58 +/-8.70 (LOS 93%) = candidato
//   (LOS<95%, da riconfermare 20s).
int g_singular_ttmargin = 3; // Singular: tt_depth >= depth - questo (:4483)
int g_singular_depth_div =
    2; // Singular: singular_depth = (depth-1) / questo (:4486)
// Q-20a (Alexandria): badNode = predicato IIR (nessuna TT move utile) -> il
// fail-high statico e' MENO contraddetto da info profonde -> margine RFP
// ridotto (prune piu' facile).
int g_rfp_badnode = 4; // rfp_margin -= questo se !tt_move. 0 = OFF
// Q-20c (Caissa Search.cpp:1786): la futility NON pota mai la PRIMA quiet del
// nodo (quietIndex>1): almeno una quiet va sempre guardata, il segnale statico
// da solo non basta a chiudere il nodo. Toggle. OFF = comportamento storico.
static bool g_fut_spare_quiet = false;
// Q-20d (Ethereal search.c:872 + move.c:576): delta-pruning qsearch col
// BEST-CASE reale (vittima piu' grossa presente + potenziale promozione) invece
// del margine fisso: se nemmeno il colpo migliore possibile porta sopra alpha,
// il nodo e' morto.
static bool g_qs_delta_bestcase = false;
int g_qs_bc_margin = 183; // cuscino sopra il best-case (scala-56, SPSA)
// Correction-history tunables (only active when CorrHist is on).
int g_corr_cap =
    48;   // BAKED 6/08/2026 col blocco corrections // max correction applied to static eval (cp). [SPSA histmulti 32->44
        // RIGETTATO: -1.3 LOS23% @11438 -> default]
int g_corr_lr_div = 303; // learning-rate divisor (bigger = slower/steadier
                         // learning) [histmulti 512->565 rigettato]
// CorrAsym (Tier-1 corrections, port SF 7a7c033a): le correzioni POSITIVE
// (nudge verso l'alto) imparano piu' lente delle negative. /128 sul peso quando
// (target-cv)>0. 128 = simmetrico = byte-identico (default). Co-tune verso
// ~100-110 (SF ~18% piu' lente). Solo con CorrHist on.
int g_corr_asym = 137;
// Continuation-history pruning tunables (only active when ContHistPrune is on).
int g_conthist_red_div =
    3684; // LMR: continuation-history reduction divisor [SPSA-tuned 5000->6595;
          // histmulti 6595->7050 rigettato]
// Aggressive-LMR tunables (only active when AggrLMR is on).
int g_aggr_lmr_div = 903; // smaller divisor -> wider reduction range
int g_aggr_lmr_clamp = 4; // max +/- ply the history reductions may apply

// ---- StatScore-LMR levers (2026-06-06) --------------------------------------
// Scoperta: vs SF15.1 SOTTO-RIDUCIAMO -> ~6 ply meno profondi a pari tempo. SF
// guida la LMR con uno statScore CONTINUO (somma history / divisore, multi-ply)
// invece del nostro clamp +/-1. Tre toggle INDIPENDENTI (default OFF = byte-
// identico), ognuno A/B-abile da solo; lo SPSA tara i divisori dopo.
static bool g_statscore_lmr =
    true; // BAKED #1 2026-06-07. StatScoreLMR: butterfly history -> riduzione
          // continua (rimpiazza il clamp +/-1)
void set_statscore_lmr(bool v) { g_statscore_lmr = v; }
static bool g_conthist_lmr =
    true; // BAKED #1 2026-06-07. ContHistLMR: conthist(1/2/4 ply) -> riduzione
          // continua, scollegata da ContHistPrune
void set_conthist_lmr(bool v) { g_conthist_lmr = v; }
static bool g_cutnode_lmr =
    true; // CutNodeLMR: riduzione extra sui cut-node (SF li riduce di piu').
          // BAKED ON 2026-06-24 (2o segnale +: on+tuned = +11 @287g su
          // Blackwell).
void set_cutnode_lmr(bool v) { g_cutnode_lmr = v; }
// ---- Threat-ordering (#2 SF, 2026-06-07) ------------------------------------
// SF ordina i quiet anche per MINACCE: muovere un pezzo da una casa attaccata
// da uno di valore INFERIORE e' buono (lo salvi), entrarci e' cattivo (lo metti
// in presa). Struttura SF (regina vs minacce-di-torre, torre vs minore, minore
// vs pedone); UNA sola costante co-tunabile (ThreatScale) invece dei ~6 magici
// di SF -> "struttura SF, costante nostra". Default OFF = byte-identico.
// Minacce calcolate 1x/nodo (cache su hash_key) -> niente costo per-mossa.
static bool g_threat_ordering = true; // BAKED FULL 2026-06-07
void set_threat_ordering(bool v) { g_threat_ordering = v; }
// ThreatHist (UCI "ThreatHist", BAKED ON 2026-07-03): la main quiet history E'
// threat-indexed — history_moves[9][12][64], bucket = from_tier*3 + to_tier
// (tier 0 safe / 1 attaccata da pedone-o- minore / 2 da torre+). td_hbucket
// ritorna il bucket quando ON, 0 quando OFF (= tabella piatta).
static bool g_threat_hist =
    true; // [BAKE 2026-07-03 secondo audit: combo ThreatHist+flow, -30.48 base
          // LOS0.06% @320g 12+0.12]
void set_threat_hist(bool v) { g_threat_hist = v; }
// CapHistThreat (UCI "CapHistThreat", default OFF): l'analogo di ThreatHist sulla
// CAPTURE history. La nostra capture_history era [pezzo][arrivo][vittima] senza
// nessuna dimensione di minaccia, mentre Reckless (history.rs:40-56) e Stormphrax
// (history.h:217) la indicizzano anche sulla casa d'arrivo difesa: la stessa
// cattura vale in modo diverso se il pezzo che arriva puo' essere ripreso.
// ⚠️ Idea genuinamente NON-SF (SF non ce l'ha) — il prior forte e' interno: e'
// l'analogo esatto di ThreatHist sulle quiet, che da noi e' bakata ON.
// OFF = byte-identico (td_cbucket ritorna 0 -> solo il piano [..][0]).
static bool g_caphist_threat = false;
void set_caphist_threat(bool v) { g_caphist_threat = v; }
// ThreatHistWeight (2026-07-04 CABLATO): scala EXTRA sul contributo della
// threat-history nell'ordering (td_score_move), applicata SOPRA MainHistWeight.
// 100 = identita' (byte-identico al release SPRT +30 combo). Cablato e
// SPRT-ato: >100 negativo (130 perde LOS87.75% @194g, run#6), <100 POSITIVO (75
// vince LOS94.46%
// @400g, run#7) — coerente: MainHistWeight basso (67) => il sistema vuole MENO
// enfasi butterfly. [BAKE 2026-07-04: 100->75, SPRT +13.90 Elo per il
// candidato, LOS 94.46% @400g 10+0.1]
int g_threat_hist_weight =
    130; // /100, scala extra del termine threat-history in td_score_move [BAKE
         // 2026-07-04 75->60, SPRT -9.39 Elo base LOS82.13% @296g + gradiente
         // SPSA concorde (discesa 75->68-73)]
int g_threat_scale =
    4212; // contributo = scale/100 * pieceValue * (from_minacciato -
          // to_minacciato). Spin ThreatScale (SPSA). Default basso = nudge
          // (1500 = +75% nodi a d20 = disturba l'ordering tarato); il co-tune
          // trova il valore nostro.
// ---- Check-ordering (#3 SF, 2026-06-07) -------------------------------------
// SF da' un bonus ai quiet che danno SCACCO DIRETTO (forcing), filtrati per non
// essere blunder (SEE >= -75). Una sola costante co-tunabile (CheckBonus).
// check_sq[] (case da cui ogni tipo di pezzo da scacco al re nemico) calcolate
// 1x/nodo. Default OFF = byte-identico.
static bool g_check_ordering = true; // BAKED FULL 2026-06-07
void set_check_ordering(bool v) { g_check_ordering = v; }
int g_check_bonus = 13357; // bonus ordering per quiet che da scacco diretto
                           // (SF=16384). Spin CheckBonus (SPSA).
// ---- Offense-ordering (port Reckless movepick score_quiet, 2026-07-24) -------
// Reckless ordina i quiet premiando le mosse che portano un pezzo su una casa
// SICURA da cui ATTACCA un pezzo nemico vulnerabile (offense[pt]), e penalizzando
// lo spostamento di un pedone-scudo del proprio re (wall_pawns). ThreatOrdering
// (escape/entra-in-minaccia) e CheckOrdering (scacco diretto) li abbiamo GIA';
// questi due termini sono l'unica parte NUOVA del loro rework. Le case-offense
// sono calcolate 1x/nodo (cache offense_key). Le due magnitudini sono spin SPSA
// separate (isolabili: l'uno a 0 isola l'altro). Costanti iniziali riportate sulla
// NOSTRA scala history dal rapporto col nostro g_check_bonus (Reckless: offense
// 3446 / check 10723; wall 4494) — poi RI-MISURATE separatamente:
// - OffenseBonus: isolato a 4300 = NEGATIVO e stabile (-9.70 +/- 10.41 Elo,
//   LOS 3.38% @1290g, 12+0.12) -> spento (0) in attesa di un peso "molto grosso"
//   da ri-testare con WallPawnPenalty gia' attivo (prossimo esperimento).
// - WallPawnPenalty: la magnitudine conta. 1x(5600)=+3.24 nElo LOS85% @12778g
//   (mai chiuso, ma affidabile); 2x(11200)=identico (+3.25 @1426g, non sensibile
//   in questo range); 3x(16800)=**+8.43 +/- 6.93 nElo, LOS 99.14%, LLR 80.5%
//   verso [0,5] @9652g (12+0.12)** — segnale nettamente piu' forte. BAKATO
//   2026-07-24 sul trend (LOS>99%, mai chiuso ma non evapora, l'opposto del
//   pattern B1) in attesa di chiusura SPRT formale e di un mega co-tune finale
//   di 6.0 per la magnitudine esatta.
// ABLAZIONE A1 (audit 8.0, 25/09/2026): senza la penalita' wall-pawn il motore NON perde, anzi:
// QuietOffense=false contro base, SPRT [-1.75, 0.25] a 10+0.1 Hash 64, +1,21 ± 2,12 Elo su 30.440
// partite (LLR ~1,5 verso H1, fermata e accettata dall'utente). Bakata a luglio sul trend con la rete di
// allora, con `legio-septima` non serve piu'. SPENTA di default; il codice resta.
static bool g_quiet_offense = false; // BAKED OFF 2026-09-25 (ablazione A1); era true dal 24/07
int g_offense_bonus = 0;       // spento: isolato NEGATIVO, vedi nota sopra
int g_wallpawn_penalty = 16800; // BAKED 2026-07-24 (3x, vedi nota sopra); era 5600
// ---- ContHist 3/6-ply (#4 SF, 2026-06-07) -----------------------------------
// SF somma la continuation-history a 1/2/3/4/6 ply; noi avevamo 1/2/4. #4
// aggiunge 3-ply (move_stack[ply-2]) e 6-ply (move_stack[ply-5]) all'ordering
// quiet, scalati da ContHist36Weight e poi dal ContHistWeight comune. Update
// sui cutoff in td_conthist_multi_update. SCOPE: solo ordering (NON la
// ContHistLMR). Default OFF = byte-identico.
static bool g_conthist36 = true; // BAKED FULL 2026-06-07
void set_conthist36(bool v) { g_conthist36 = v; }
int g_conthist36_weight = 37; // /100: peso dei termini 3/6-ply relativo agli
                              // altri conthist. Spin ContHist36Weight.
// ---- V2: prior-counter-move bonus + capture-history bonus (SF, 2026-06-07)
// -------- Su un nodo FAIL-LOW (nessuna mossa batte alpha) la mossa PRECEDENTE
// (quella che ha portato qui) era forte (ci ha messo in difficolta') -> bonus
// alla sua history. Quiet: main + continuation history; cattura: capture
// history (vittima da captured_stack). Una sola scala co-tunabile
// (PriorBonusScale) sul td_stat_bonus. Default OFF = byte-identico.
static bool g_prior_bonus = true;
void set_prior_bonus(bool v) { g_prior_bonus = v; }
int g_prior_bonus_scale =
    189; // /100 del td_stat_bonus(depth). Spin PriorBonusScale.
// ---- R-PB: il prior bonus di Reckless (src/search.rs:1123) in due pezzi separabili.
// Oggi noi premiamo la mossa precedente su OGNI all-node e con un bonus PIATTO. Loro
// fanno due cose diverse, e sono indipendenti, quindi qui sono due toggle distinti da
// misurare uno per volta. Entrambi default OFF = byte-identico.
//
// (1) g_prior_bonus_gate — premia solo gli all-node INATTESI (`cut_node || pv_node`).
//     Se il nodo doveva fallire basso e ha fallito basso, la mossa precedente non ci ha
//     detto niente di nuovo: il bonus e' rumore. E' anche il finding B7 del nostro audit
//     ("PriorBonus non gated"), rimasto aperto.
//
// (2) g_prior_bonus_factor — scala il bonus su QUANTO il fail-low e' stato una sorpresa:
//     quante mosse aveva gia' provato il padre, se la mossa era la sua TT-move, e due
//     misure di quanto lo score e' crollato sotto l'eval.
//     ⚠️ La scala non e' la loro: applicare `factor/128` sopra il nostro bonus attuale
//     (~5568 su HISTORY_MAX 7000) manderebbe OGNI update contro il clamp, cancellando
//     proprio la scalatura che si vuole introdurre. `g_pb_scale` e' tarato perche' il
//     bonus MEDIO resti quello di oggi: fattore medio ~480, 480*50/(100*128) = 1,88 volte
//     td_stat_bonus contro le 1,89 attuali. Cambia la FORMA, non il livello.
static bool g_prior_bonus_gate = false;
void set_prior_bonus_gate(bool v) { g_prior_bonus_gate = v; }
static bool g_prior_bonus_factor = false;
void set_prior_bonus_factor(bool v) { g_prior_bonus_factor = v; }
int g_pb_scale = 50;       // /100, poi /128 col fattore: preserva il bonus medio
int g_pb_base = 88;        // termine costante del fattore
int g_pb_mc = 17;          // per mossa gia' esaminata dal padre
int g_pb_mc_cap = 229;     // cap del termine sopra
int g_pb_ttm = 110;        // la mossa precedente era la TT-move del padre
int g_pb_ev = 144;         // score crollato sotto la nostra eval
int g_pb_ev_marg = 97;     // di quanto
int g_pb_swing = 306;      // score crollato sotto l'eval NEGATA del padre (swing grosso)
int g_pb_swing_marg = 136; // di quanto
// ---- #5: low-ply history (SF)
// ----------------------------------------------------- History per-ply usata
// SOLO nei primi LOW_PLY_MAX ply (vicino alla radice): cattura "questa mossa e'
// buona a questo ply basso". Peso /(1+ply). Default OFF = byte-identico.
static bool g_lowply = true;
void set_lowply(bool v) { g_lowply = v; }
int g_lowply_weight =
    179; // contributo ordering = g_lowply_weight * lowply / (100*(1+2*ply))
         // (SF-style decay). Spin LowPlyWeight.
// StatEvalDiffOrder (SF search.cpp ~728, "~9 Elo"): dopo la static eval, spinge
// la history (main + pawn) della mossa PRECEDENTE (dell'avversario) col negato
// della SOMMA delle due static-eval consecutive — segnale a costo ~0 che quella
// mossa ha prodotto uno swing di eval buono/cattivo. NUOVA feature aggiunta
// SOPRA il parco mega-SPSA (gli altri ordering-weight restano tunati). Spin
// "StatEvalDiffMult": 0 = OFF (byte-identico al 4.0), 14 = SF-esatto, >14 =
// piu' aggressivo di SF.
int g_seo_mult = 27; // [4.1 BAKE 14->6] (Ripristinato a 14 dopo fix SEO)
// cutoffCnt (SF): contatore fail-high per-ply. In LMR riduce di piu' se il
// figlio ha cuttato molto di recente. g_cutoffcnt_penalty: 0=off (default,
// byte-identico), 1=SF (r+=1 se cutoff_cnt figlio > 3). Spin CutoffCntPenalty
// [0,3]. Co-tunabile.
int g_cutoffcnt_penalty = 2;
// ==== F-018 (SECONDO AUDIT 2026-07-03): micro-tecniche da Obsidian/Berserk
// ==== TUTTE default OFF/neutro = byte-identico (bench-verificato). Una leva =
// un toggle/spin, testabili una alla volta o co-tunate a blocchi. Dettagli:
// Triumviratus_5/SECOND_PASS_FEATURES.md.
static bool g_postlmr_hist =
    false; // #1 bonus/malus conthist dopo la re-search post-LMR (mainline SF).
           // Il "+18.55" era same-binary; test PULITO build-matched (current vs
           // 549b737, 8+0.08) = -3.50 NEUTRO -> tenuto OFF, candidato a tuning
           // futuro (forse TC-dipendente).
int g_postlmr_scale = 106; //    /100 del td_stat_bonus applicato
static bool g_ttcut_refine =
    true; // #3a-c cutoff TT: depth+1 sui fail-high, coerenza cutnode,
          // fifty<soglia [BAKE 2026-07-03]
int g_ttcut_fifty =
    89; //    soglia fifty oltre cui il cutoff TT non e' affidabile (patta-50
        //    vicina) [BAKE 2026-07-04 blocco secondaudit_block SPSA, SPRT +8.34
        //    Elo LOS79.4% @500g (non conclusivo ma leaning+)]
// Q-14 TTResearchMargin (Ethereal, tecnica di Grant): fail-low anticipato a
// depth-1. Entry UPPER (hash_flag_alpha) a tt_depth>=depth-1 -> return se !PV
// && (cutnode || ttScore<=alpha) && ttScore+margine <= alpha. Duale del
// TTCutRefine (+1ply ai fail-high). Margine 141 Ethereal ~= 79 alla nostra
// scala-56 (init SPSA). Default OFF.
static bool g_tt_research = false;
int g_tt_research_margin = 74;
// ===== IDEE DA CODA 0.9.4 (27/09/2026, docs/audit_8.0/J_CODA.md). Tutte spente di default: albero identico. =====
// (1) RootDepthRelax: potare MENO quando la ricerca complessiva e' profonda. Guarda td.root_depth (l'iterazione
//     in corso), non la profondita' del nodo: e' la differenza con LMRDeepK/LMRSFBase, che erano neutri/negativi.
//     over = max(0, root_depth - RDRKnee). Coda: RFP margin += depth*over*20/100 (search.rs RFP_ROOT_*). Da noi
//     anche LMP e ProbCut nello stesso verso (noi siamo tarati a TC corto; Coda li ha nel verso opposto perche' la sua
//     base e' piu' vicina al TC lungo). Valori da SPRT: RDRRfp 20, RDRLmp 5, RDRProbCut 5.
int g_rdr_knee = 17;      // profondita' di radice oltre cui si allenta
int g_rdr_rfp = 0;        // RFP: margine += rfp_depth * over * RDRRfp / 100 (cp)
int g_rdr_lmp = 0;        // LMP: soglia di mosse += over * RDRLmp / 10
int g_rdr_probcut = 0;    // ProbCut: beta di probcut += over * RDRProbCut (cp)
// (2) TTNearMiss (Coda FEAT_TT_NEARMISS): taglio da TT con entry di UN ply piu' corta, se lo score supera il
//     bound di almeno TTNearMiss cp (non-PV, niente score decisivi, niente patta-50 vicina). 0 = off; Coda 80.
//     Il lato fail-low esiste gia' da solo (TTResearch/TTResearchMargin, Ethereal): questo fa entrambi.
int g_tt_nearmiss = 0;
// (3) TTDamp (Coda TT_DAMP_TT_WEIGHT_10X): nel taglio TT non-PV con bound inferiore, restituisce
//     (W*tt_score + 10*beta) / (W+10) invece di tt_score (score meno gonfiati). W in decimi; 0 = off; Coda 31.
int g_tt_damp = 0;
// ===== Dallo studio dei riferimenti sulla profondita' di radice (27/09/2026, docs/audit_8.0/K_RIFERIMENTI_LTC.md) =====
// PvTTMinDepth: nella PV la mossa TT non scende in quiescenza (SF, Reckless, Caissa, PlentyChess). Default off.
static bool g_pv_tt_min_depth = false;
// RootReplyRedPct (28/09/2026): % della riduzione LMR tenuta sulle risposte dell'avversario alla mossa di radice.
// 100 = spento. Continua da 0 a 100: tarabile con SPSA se l'SPRT da' segnale.
int g_rr_red_pct = 100;
// ===== Idee nostre dalle partite CCRL (28/09/2026), tutte spente di default =====
// NullThreatExt: margine (cp) oltre cui una null fallita bassa estende il nodo di 1 ply (12_negamax.inc). 0 = off.
int g_null_threat_ext = 0;
// TMDrift: % di tempo in piu' sulla deriva lenta della valutazione (14_smp.inc). 0 = off.
int g_tm_drift = 0;
int g_tm_drift_thresh = 40; // calo totale minimo (cp) sulle ultime TMDriftMoves mosse
int g_tm_drift_moves = 8;
// (BucketUncert, incertezza dal bucket vicino della rete: rimandata. Serve portare la divergenza attraverso eval
// cache e TT, che oggi salvano solo la eval; senza, il segnale manca a ogni hit. Sonda EvalBucketOverride del
// 17/08: salto mediano 14 cp fra bucket adiacenti, max 49, misura su aperture.)
static bool g_ttcut_malus =
    false; // #3d malus alla quiet AVVERSARIA che porta a un TT-cut (duale di
           // TTCutBonus). BAKE 2026-07-06 (true/7) REVERTITO: SPRT +15.55 Elo
           // era falsato da oversubscription/TC senza incremento; retest pulito
           // (10+0.1, 258g) = -17.52 Elo, LOS 4.89%, LLR non conclusivo.
           // Riaperto, serve retest pulito prima di ridecidere.
int g_ttcut_malus_seen = 3; //    solo se il padre aveva visto <= N mosse
// #4 split good/bad capture a soglia -(mvv+caphist)/div (0=off, Obsidian 32).
// STORIA (2026-07-13/14): screening VM +6.96 @10+0.1 (LOS 97%) → bakato 32 →
// gate-blocco a 20+0.2 NEUTRO (+2.4 ±6.4 su 2606g) → REVERTITO a 0: cambia
// l'albero (bench 377398→487874) e COMPRIME il segnale dei gate-rete durante
// il training v2 (serie epoca-vs-epoca non piu' comparabile). Resta candidato
// del MEGA CO-TUNE post-v2 (li' i margini si ri-coordinano attorno allo split).
int g_goodcap_hist_div = 32; // BAKED 2026-07-24 (bundle lean SPRT +10.43 Elo; era 18)
static bool g_asp_avg =
    false; // #5a aspiration centrata sulla MEDIA degli score. Bake 2026-07-07
           // REVERTITO: -9.04 Elo era singolo draw + winner's curse; pacchetto
           // cumulativo pulito = +0.62 NULLO. OFF, re-ispezione a TC lungo
           // (TC-dipendente?)
static bool g_asp_fhred =
    true; // #5b root: depth-1 per ogni fail-high consecutivo (evita re-search
          // piene) [BAKE 2026-07-03]
int g_singular_mindepth =
    6; // #6a gate depth del blocco singular (8=storico; Obsidian 5, Berserk 6)
       // [BAKE 2026-07-04 blocco secondaudit_block SPSA, SPRT +8.34 Elo
       // LOS79.4% @500g (non conclusivo ma leaning+)]
int g_singular_de_cap =
    1; // #6b cap catena double-extension sul path (0=off; Berserk 6) [BAKE
       // 2026-07-04 blocco secondaudit_block SPSA, SPRT +8.34 Elo LOS79.4%
       // @500g (non conclusivo ma leaning+)]
static bool g_singular_plyguard =
    false; // #6c niente singular oltre ply >= 2*rootDepth (anti-esplosione)
// #6d negext se ttScore <= alpha (0=off; Berserk 1). [BAKE 2026-07-04 a 1, blocco
// secondaudit_block SPSA, SPRT +8.34 Elo LOS79.4% @500g, non conclusivo]
// 🔴 BAKED A 2 il 7/08/2026: **+6,50 +/- 5,77 Elo, LOS 98,64%, 3958 partite
// @30+0.3**, un solo parametro di differenza. L'intervallo [0,7 ; 12,3] esclude
// lo zero. Non portato al bound: a 30+0.3 la risposta era gia' leggibile.
// 🔑 ATTRIBUZIONE, da non confondere leggendo il log. Il ramo MORTO trovato quel
// giorno era `NegExtCut` (irraggiungibile: i due rami sopra partizionano lo
// spazio in finestra nulla), e riattivarlo col riordino NON ha pagato — nel
// gauntlet a sei bracci tutte le configurazioni col riordino stavano SOTTO la
// base. A pagare e' questo parametro, che era sempre stato vivo e non era mai
// stato messo in discussione. Quello che ha funzionato non e' la scoperta del
// ramo morto in se': e' che indagarlo ha portato ad auditare l'intera famiglia.
// ⚠️ Il gauntlet metteva sopra la base sia `Alpha=2` sia `Alpha=0`, cioe' due
// modifiche OPPOSTE: uno dei due era rumore. Il testa a testa ha confermato il 2.
// Resta non provato il 3, che e' il resto del range: candidato per un altro giro.
int g_negext_alpha = 2;
static bool g_fh_smooth =
    true; // #7 fail-high: return (score+beta)/2 su RFP e qsearch (bound TT meno
          // gonfiati) [BAKE 2026-07-03]
// FailHighSmooth, PESI SEPARATI (2026-07-25). Il midpoint era `(x+beta)/2`
// cablato su TRE ritorni semanticamente diversi, sotto un unico toggle e senza
// nessun parametro. Nessuno dei riferimenti fa cosi': Reckless (search.rs:1437
// `lerp`) usa CINQUE t distinti e tunati — 0.6945 su RFP, 0.8256 sullo stand-pat
// di qsearch, 0.5072 sul finale di qsearch, 0.2695 su NMP, 0.4027 su multicut —
// e Stormphrax ne espone due tunabili a SPSA. Due implementazioni indipendenti
// convergono su valori LONTANI dal nostro 0.5, e lo stand-pat di qsearch e' il
// sito piu' eseguito del motore.
// Scala /1024: 512 = midpoint = comportamento storico.
int g_fh_t_qs_standpat = 512; // Reckless: ~845/1024
int g_fh_t_qs_final = 512;    // Reckless: ~519/1024
int g_fh_t_rfp = 512;         // Reckless: ~711/1024
// ⚠️ A t = 512 si ritorna l'ESPRESSIONE ORIGINALE, non la lerp equivalente: con
// x+beta negativo e dispari le due troncano diversamente (es. x=-4, beta=-1 ->
// (x+beta)/2 = -2 ma x+(beta-x)/2 = -3). Il caso speciale garantisce che il
// default sia byte-identico; fuori dal default la lerp e' la forma giusta.
static inline int td_fail_firm(int x, int beta, int t) {
  if (t == 512)
    return (x + beta) / 2;
  return x + (int)((long long)(beta - x) * t / 1024);
}
static bool g_nmp_static_margin =
    false; // #8a NMP solo se static_eval >= beta - mult*depth + bias (SF)
int g_nmp_sm_mult = 23;
int g_nmp_sm_bias = 414;
static bool g_nmp_ttnoisy =
    false; // #8b R+1 se la TT move e' una cattura (Obsidian)
static bool g_alpha_depth_dec =
    true; // #9 depth-- per le mosse restanti quando una mossa alza alpha
          // (2<=depth<=11) [BAKE 2026-07-03]
int g_fhboost_margin =
    2; // #10a bonus history a depth+1 se best > beta+margine (Obsidian 95,
       // Berserk 75). Bake 2026-07-07 (margin=50) REVERTITO: i "conferma" 2x
       // avevano AspAvg ON; retest ISOLATO vs baseline pura = +1.74 NULLO. OFF
       // (0), re-ispezione a TC lungo
static bool g_hist_triv_guard = false; // #10b niente bonus al cutoff "gratis"
                                       // (depth<=3, nessun quiet prima)
int g_malus_pct =
    69; // #10c malus = bonus * pct/100 (costanti malus separate dal bonus)
static bool g_easycap_gate =
    false; // #11 niente NMP se abbiamo un pezzo in presa "facile" (Berserk)
int g_rfp_hist_thresh =
    64; // #12 RFP solo se hash-move non-quiet o con history > soglia (0=off)
static bool g_killer_reset =
    true; // #13 azzera i killer del ply FIGLIO a ogni nodo (anti-stale, +12.15 Elo)
// ⭐ CounterMove (default ON = comportamento storico, byte-identico). Toggle di
// RIMOZIONE: SF ha SEMPLIFICATO VIA la countermove heuristic (a45c2bc3, PR
// #5441, lug-2024) con un test non-regressivo da **978.000 partite STC** + 81k
// LTC: da loro valeva ZERO. Il movepicker di SF master non ha piu' lo stage
// refutation-countermove. Noi lo abbiamo ancora, di default. ⚠️ NON
// confondere con `g_prior_bonus` (bonus history alla prior countermove sul
// fail-low): quello SF lo TIENE e lo tuna ancora. Qui si spegne solo la TABELLA
// counter_moves e il suo stage. Movente: 19/07 il pattern e' "togliere paga,
// aggiungere no" (10 patch, 9 bocciate; l'unica vincente +7.98 toglieva la
// check-extension obsoleta).
static bool g_countermove = true;
// QSCapRecap (spin 0/1, default 0 = byte-identico). Oltre il cap di QSMoveCap la
// RICATTURA sulla casa appena mossa dall'avversario non viene piu' tagliata.
// Le altre due potature-per-volume dello stesso move loop hanno gia' questa
// esenzione (BadNoisy, QFutility); QSMoveCap e' la piu' aggressiva delle tre
// (3 contro 7) ed e' l'unica senza. Sulla QFutility la sua assenza costava
// -17 Elo. Ortogonale a PromoQS: quello decide chi CONSUMA uno slot, questo
// decide chi PASSA a slot esauriti.
int g_qs_cap_recap = 0;
int g_qs_move_cap =
    3; // #14 cap mosse esaminate in qsearch non-in-check (0=off; Obsidian 3).
       // BAKED 2026-07-25: era 1, cioe' la qsearch cercava UNA sola mossa per
       // nodo (una CATENA, non un albero) — reperto dell'audit finale 6.0.
       // Portato a 3 (il valore di Obsidian): **+5.21 +/- 6.47 Elo, LOS 94.28%,
       // 2868 partite @15+0.15**. Bakato su LOS, non su bound SPRT chiuso.
// ==== Bug-fix a toggle (cambiano i node-count -> SPRT prima di bakare ON) ====
static bool g_qs_draw_check =
    false; // F-015: draw-detection (ripetizione/50 mosse) in qsearch. Bake
           // 2026-07-07 REVERTITO: -10.73 Elo era singolo draw + winner's
           // curse; pacchetto cumulativo pulito = NULLO. OFF, re-ispezione a TC
           // lungo
bool g_ep_key_fix = true; // F-017: ep nella hash key SOLO se la cattura ep e'
                          // pseudo-legale (anche movegen.cpp) SPRT 16+0.16 1t
                          // 128MB UHO_4060_v4: +4.31 ±4.37 Elo, LOS 97.35%,
                          // 6282g fix di correttezza → default ON (2026-07-21).
// QsStalemateCheck (SF d2d046c-style, default OFF, mai testato 2026-07-24):
// qsearch genera SOLO catture (+ eventuali check) quando non e' sotto scacco,
// quindi non puo' MAI accorgersi che una posizione e' stallo (zero mosse
// legali, tranquille comprese) — la restituisce come un eval normale. Il buco
// e' concreto nei finali K+P (trucchi di stallo comuni). Gated alla sola
// transizione rara "questa cattura ha appena tolto l'ultima Torre/Donna dalla
// scacchiera" (vedi td_has_any_legal_move): un probe di legalita' a
// movegen-completo li' e' economico (il caso vero-stallo che deve scandire
// TUTTA la lista e' proprio quello raro che vogliamo scovare).
static bool g_qs_stalemate_check = true; // BAKED 2026-07-24 (bundle lean SPRT +10.43 Elo)
// Syzygy{ProbeDepth,ProbeLimit,50MoveRule} (2026-07-24): 3 opzioni UCI
// advertised (uci_mt.cpp) fin dalla release ma MAI wired — bug scoperto in
// audit (FASE C hygiene, PIANO_MASTER §5): una GUI che le settasse non
// avrebbe avuto alcun effetto. Default = comportamento storico ESATTO (depth
// gate sempre soddisfatto perche' qui depth>=1 gia' garantito dal caller;
// limit 7 = TB_LARGEST massimo di Syzygy, mai piu' stretto; 50MoveRule=true =
// cursed/blessed restano 0 come sempre) -> nessuna regressione col default.
int g_syzygy_probe_depth = 1;
int g_syzygy_probe_limit = 7;
// CutoffStats (diagnostica move-ordering, default OFF = byte-identico): se ON,
// conta sui beta-cutoff il first-move-cutoff rate (fh_first/fh_nodes) + indice
// medio della mossa al cutoff, e li stampa come "info string FMC ..." a fine
// ricerca. Il gap vs SF e' l'ordering (first-move-cutoff storico 84.76% SATURO)
// -> questo lo misura per guidare il lavoro futuro.
bool g_cutoff_stats = false;
// ProbCut-sotto-scacco (SF step 12): in scacco, se la TT ha una cattura con
// bound LOWER e score >= beta+margin a depth>=depth-4, ritorna beta+margin.
// 0=off (default). Spin ProbCutInCheckMargin [0,800] (SF=452). Co-tunabile.
int g_probcut_incheck_margin = 331; // [4.1 BAKE 0->523]
int g_lmr_ss_div =
    5430; // [4.1: tenuto 4.0 - il BAKE @12s 13790 GONFIAVA l'albero 2.5x (meno
          // riduzione) per ~pochi Elo: scelta = albero stretto > Elo]
int g_lmr_ss_offset = -2953; // [4.1: tenuto 4.0 - vedi sopra; il BAKE -2214 =
                             // parte del bloat LMR, revertito]
int g_lmr_ch_div = 10942;    // [3.7 BAKE 4437->3506; BAKED #1 era 10000].
                             // ContHistLMR: reduction -= (conthist1+2+4) / div
int g_cutnode_lmr_extra = 1; // CutNodeLMR: ply extra di riduzione sui cut-node
                             // (sopra il +1 esistente)
// NMP + LMR-enrichment tunables.
int g_nmp_base = 5; // null-move reduction: R = g_nmp_base + depth/g_nmp_div
int g_nmp_div = 3;
int g_lmr_eval_margin =
    43; // [F-004/005 BAKE 2026-07-02: 33->20, SPSA in blocco LMR] LMR: reduce
        // +1 more when static_eval + margin < alpha
int g_lmr_ttdepth =
    1; // LMR: reduce LESS by this when TT depth >= depth   [SPSA-tuned: 0->2]
// CORE LMR formula coefficients (*100).
int g_lmr_base_x100 = 22; // baseline reduction floor [3.7 BAKE 41->37; SPSA
                          // 75->47; BAKED #1 47->41]
int g_lmr_div_x100 = 447; // bigger divisor = LESS reduction [3.7 BAKE 345->310;
                          // SPSA 225->270; BAKED #1 270->345]
// LMRSFBase: vedi il commento accanto a lmr_sf_table (init_lmr_table).
int g_lmr_sf_base = 0;
int g_lmr_sf_mult = 2244; // x100: SF reductions[i] = int(2872/128 * ln i) = int(22,44 ln i)
int g_lmr_sf_off = 982;   // 1/1024 ply: il +982 di SF reduction()
// LMRDeepK (studio finali, variante 1, 25/09/2026). LMRSFBase ha perso -33 a 40+0.4: la base di SF
// sopra i NOSTRI termini riduce troppo ovunque. Qui si tocca solo la zona che lo SPSA a TC corto non
// ha mai visto: sopra profondita' LMRDeepD0 si aggiunge  k * (ln d - ln d0) * ln(mossa, da 1)  in
// 1/1024 di ply. k = 276 porta la nostra pendenza in profondita' a quella di SF 19 (~0,49 contro
// 0,22 per unita' di ln d * ln m); sotto d0 non cambia nulla. 0 = off, byte-identico.
int g_lmr_deep_k = 0;
int g_lmr_deep_d0 = 12;
// ⭐ 5.1 STRUTTURALE — riduzione LMR "FINE" stile-SF in 1/1024 di ply (default
// OFF = byte-identico). La nostra LMR monta la riduzione in PLY INTERI (±1) =
// grezza: tagliando di piu' si sovra-pota OVUNQUE (l'SPSA del cut-block era
// piatto perche' la magnitudine non discrimina DOVE tagliare). SF
// (search.cpp:1287-1334) la monta in millesimi di ply fondendo ~12 segnali con
// peso fine, e taglia FORTE solo dove e' sicuro (cut/ALL-node, history pessima)
// proteggendo PV/ttPv/eval-incerta
// -> piu' riduzione TOTALE = piu' profondita' SENZA perdere tattica.
// Coefficienti in 1/1024 ply, SPSA-tunabili.
static bool g_lmr_fine = true; // 5.1 BAKE (spsa_struct iter1216, media ultimi
                               // 100 @20+0.2): ON di default
// 🔴 08/09/2026 — vettore rimesso ai valori PRE-SPSA. Lo SPRT di validazione del
// vettore tarato (13.192 partite, 12+0.12 hash 64) da' **-2,08 ± 3,13** per il
// vettore nuovo: lo zero e' dentro la banda, quindi non c'e' evidenza a favore
// del cambio, e questi otto valori sono quelli tarati storicamente.
// Vettore SPSA rifiutato: SS=695 All=557 Cut=4629 Cutoff=1655 Improv=356
//                         Corr=897 Ply=426 Killer=764
int g_lmrf_cut =
    3687; // [5.1 BAKE 3995->3184] cut-node: riduzione forte (il divario #1)
int g_lmrf_cut_nott =
    2397; // [5.1 BAKE 1059->2092] extra sui cut-node senza TT move
int g_lmrf_ttcap =
    200; // [5.1 BAKE 1039->390]  ttMove e' una cattura -> riduci di piu'
int g_lmrf_ttpv =
    617; // [5.1 BAKE 2766->2210] ttPv: riduci MENO (protezione del ramo ex-PV)
int g_lmrf_pv = 437; // [5.1 BAKE 1017->1551] PV node: riduci MENO
// ⭐ BAKED ON (2026-09-07): vettore SPSA a 8 parametri, SPRT +2,08 +/- 3,13 su 13.192
// partite a 12+0.12 hash 64 (LLR 0,64, stabile su dodici report consecutivi fra +1,63 e
// +2,74). Tarato da uno SPSA a 10.006 iterazioni / ~40.000 partite, 8 parametri con
// perturbazione al 10% del range -- il regime che il Mega-SPSA (124 param, c al 4%) non
// aveva. Valori precedenti: ss 664, all 618, cut 3687, cutoff 1520, improv 489,
// corr 866, ply 524, killer 797.
// 🔴 QUESTO CAMBIA LA FIRMA DEL BENCH: 252074 non e' piu' il canary.
// ⚠️ Tarato E misurato entrambi a 12+0.12 hash 64. La regola di regime (§1.11) vuole la
//    conferma a hash 256: TTTwoLevel valeva +4,55 a hash 64 e zero a 256.
int g_lmrf_ss = 664; // [5.1 BAKE 445->923]   statScore (history continua): r -=
                     // statScore*g/4096
int g_lmrf_corr =
    866; // [5.1 BAKE 160->478]   correctionValue (eval incerta -> riduci meno)
int g_lmrf_all = 618;     // [5.1 BAKE 272->723]   scaling ALL-node: r +=
                          // r*g/(256*depth+285)  [ci mancava]
int g_lmrf_improv = 489;  // [5.1 BAKE 1024->411]  non-improving
int g_lmrf_evalcut = 979; // [5.1 BAKE 1024->1517] eval+margin < alpha
int g_lmrf_cutoff =
    1520; // [5.1 BAKE 1100->1626] figlio con cutoffCnt alto: riduci di piu'
// ⭐ LmrAlphaGap (port SF `5f7348f0`, 19/08/2026 — "Reduce LMR less
// aggressively in loose alpha windows"). SF: STC LLR 2.95 su 90.784 partite
// <0.00,2.00>, LTC LLR 2.94 su 209.640 <0.50,2.50> — l'unica patch da GUADAGNO
// della finestra 01/08-06/09, passata su ENTRAMBI i regimi.
//
// Il meccanismo: la riduzione diventa funzione CONTINUA del divario fra alpha e
// la eval statica. Noi qui abbiamo solo il GRADINO `g_lmrf_evalcut` (binario:
// eval+margine < alpha), che e' lo stesso segnale quantizzato a un bit. Questa
// e' la rampa.
//
// Il clamp e' ASIMMETRIC0 apposta (SF: -64 / +96): punisce la mossa quiet la cui
// eval sta sotto alpha piu' di quanto premi quella che ci sta sopra.
//
// ⚠️ Le tre costanti sono tarate sulla rete e sulla scala di eval di SF: da noi
// vanno RITARATE, non ereditate (e' l'errore gia' pagato con `de948f0f`). Per
// questo sono spin e non costanti.
// 0 = OFF = byte-identico (canary bench 252074).
int g_lmr_alpha_gap = 0;  // moltiplicatore in 1/1024 di ply. SF usa 3.
