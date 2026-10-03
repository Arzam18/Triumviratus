// search/01_params_globals.inc -- Globali della ricerca e parametri base.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// GLOBALS
// ============================================================================

tt_entry *hash_table = nullptr;

// Stockfish piece codes indexed by Triumviratus piece
// (P,N,B,R,Q,K,p,n,b,r,q,k):
//   white W_PAWN..W_KING = 1..6, black B_PAWN..B_KING = 9..14. Defined here
//   (before td_make_move) so the make-move NNUE mirror hook can use it too.
static const int nn_piece_code[12] = {1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14};

// POLICY-NET: RIMOSSA dal motore. Due volte, per due ragioni diverse.
//
// 2026-06-11 (prima rimozione): ordering -85 nei nodi interni, -30 alla radice,
// seeding offline -35, EntropyTM -16,7 LOS 5%, PolicyEasyMove -14,35 LOS 0,11%.
// Tutte le misure erano pero' confondibili con IL COSTO: quella policy era una
// CNN e il suo forward alla radice costava 18 ms, il 10-15% del budget di ogni
// mossa a TC corto.
//
// 2026-08-22 (seconda rimozione): quella scusa non c'e' piu'. Rifatta con primo
// strato sparso e uscita pigra, forward a 16-33 ns/mossa, degrado NPS misurato
// **-0,79%** — praticamente gratis — e allenata sulle NOSTRE partite invece che
// su T80 (top-1 35,99%, top-3 62,26% con 10,5 M parametri). I ganci restano
// negativi lo stesso: PolicyLMP -22,83 +/- 8,20, PolicyTT -46,89 +/- 33,99,
// PolicyOrder invariato. L'unico non-negativo, PolicyRootLmr, si e' fermato a
// +1,36 +/- 2,71 su 7.400 partite (LLR 0,44): il valore SCENDE mentre la banda
// si stringe, cioe' tende a zero.
//
// 🔴 LA RAGIONE, MISURATA: il first-move-cutoff senza TT e' al **79,2%** grazie
// a continuation history a 6 ply + killer + countermove. Un prior statico con
// top-1 al 36% non puo' migliorarlo, puo' solo disturbarlo. Non e' un problema
// di qualita' della rete ne' di costo: e' che l'informazione che porta il motore
// ce l'ha gia', piu' precisa e aggiornata per-sottoalbero.
//
// Codice, reti e misure complete: Archivio_Policy_7.0/. Non riaprire il capitolo
// senza aver letto quel LEGGIMI: le due trappole (indicizzazione a8=0 vs a1=0 del
// trainer, buffer quiet del MovePicker riusato fra i nodi) sono costate giorni.

// ---- Bundle 3.9 (2026-06-11): micro-fix "correttezza/gratis"
// (P1.4/P1.11/P1.13/ P1.10a/P2.1 dell'analisi). Ognuno dietro toggle default
// ON, ablazione stile 3.8.

// P1.4 Mate-distance pruning: stringe [alpha,beta] coi matti gia' provati piu'
// corti (un matto piu' lungo non puo' migliorare il risultato). 3 righe,
// rischio ~0.
static bool g_mate_dist = true;
void set_mate_dist(bool v) { g_mate_dist = v; }

// P1.11 Draw dither (SF value_draw): le patte per ripetizione/50-mosse
// ritornano ±1cp in funzione del node-count invece di 0 secco -> rompe la
// cecita' da ripetizione (evita oscillazioni 0.00 premature fra linee
// "ugualmente patte").
static bool g_draw_dither = true;
void set_draw_dither(bool v) { g_draw_dither = v; }

// P1.13 History bonus al ttMove quiet su TT-cutoff (SF): un cutoff servito
// dalla TT non passa dal loop mosse -> senza questo la history del ttMove si
// raffredda anche se la mossa continua a tagliare. Scala SPSA-tunable
// (TTCutBonusScale /100).
static bool g_ttcut_bonus = true;
void set_ttcut_bonus(bool v) { g_ttcut_bonus = v; }
int g_ttcut_bonus_scale =
    115; // [PR1 122->115] [M20 111->122] /100 del td_stat_bonus(depth); spin "TTCutBonusScale"

// P1.10a TT age-refresh al probe-hit: un hit rinfresca l'age dell'entry, cosi'
// le posizioni CALDE ma scritte in search vecchie non vengono evictate per
// anzianita' (SF fa lo stesso). Definito qui, extern in tt.h (usato da
// probe_tt).
bool g_tt_age_refresh = true;
void set_tt_age_refresh(bool v) { g_tt_age_refresh = v; }

// P2.1 Pawn key INCREMENTALE: td.pawn_key mantenuta in make/unmake (XOR dei
// soli pedoni mossi/catturati/promossi, self-inverse come OccIncr) al posto del
// rescan delle bitboard pedoni a OGNI td_corr_index (corr 1x/nodo +
// pawn_history ~30 quiet/nodo). NODE-IDENTICAL per costruzione -> misura NPS a
// depth fissa.
static bool g_pawn_key_incr = true;
void set_pawn_key_incr(bool v) { g_pawn_key_incr = v; }

// CorrNonPawn: np_key[2] (Zobrist non-pedoni PER COLORE) incrementale in
// make/unmake, stesso schema self-inverse di P2.1. Mantenuta SEMPRE (costo ~2-6
// XOR/mossa) cosi' il toggle CorrNonPawn puo' accendersi al volo senza chiavi
// stale. NPKeyIncr=false = fallback rescan per l'oracle node-identity (deve
// dare nodi IDENTICI a parita' di CorrNonPawn, per costruzione).
static bool g_np_key_incr = true;
void set_np_key_incr(bool v) { g_np_key_incr = v; }

// ---- Wave 3b (2026-06-11): P1.1 static-eval-in-TT + P2.2 + P2.3 -------------

// P1.1 (UCI "TTStaticEval", default ON): usa l'eval statica salvata nella TT
// (16 bit PIENI in `ext`, vedi tt.h:40-51 — il commento diceva "8 bit, ±508cp
// passo 4" ma quella era la v1: dalla 4.0 la quantizzazione non c'e' piu')
// al posto della forward NNUE quando il
// probe la porta. Il motore e' EVAL-BOUND (58% del tempo-nodo = forward): ogni
// hit con eval = una forward risparmiata. Errore ±2cp + staleness-fifty
// accettati (stessa classe dell'eval-cache, gia' ON). Lo STORE dell'eval e'
// incondizionato (gratis); il toggle gata solo l'USO. Cambia i node-count ->
// SPRT.
static bool g_tt_static_eval = true;
void set_tt_static_eval(bool v) { g_tt_static_eval = v; }

// P1.1 FIX fifty-staleness (2026-06-11): l'eval del wrapper SF e' SMORZATA dal
// rule50 (evaluate.cpp: v*(200-fifty)/214) -> salvarla cruda in TT (chiave
// SENZA fifty) la rende stantia: stored a fifty basso, riusata a fifty alto =
// eval inflazionata (misurato: -37% nodi startpos = over-pruning; +1328% nel
// finale bloccato = caos+corr avvelenata). Come SF (unadjustedStaticEval): in
// TT va l'eval DE-smorzata; all'uso si RI-smorza col fifty corrente. Errore
// residuo = solo arrotondamenti interi (~±3cp) invece di v*Δfifty/214 (fino a
// ~237cp).
// Rule50Formula (spin 0/1, default 0 = comportamento attuale, byte-identico).
// 🔴 DISALLINEAMENTO trovato il 7/08/2026. Le due funzioni qui sotto invertono
// `v*(200-fifty)/214`, che e' la formula di una versione PRECEDENTE del wrapper SF
// (ed e' quella che il commento sopra cita). Lo smorzamento realmente applicato da
// `nn_scale` e' un altro: `v -= v*rule50/199`, cioe' `v*(199-rule50)/199`
// (nnue_bridge.cpp:181, e identico in nn_finalize :283).
// Le due funzioni restano l'una l'inversa dell'altra, quindi il round-trip e'
// auto-consistente e — con TTEvalNoDecay bakato — NON si compone. Il difetto e'
// semantico: un'eval salvata a fifty=s e riletta a fifty=n da'
// `raw*(200-n)/(200-s)` invece di `raw*(199-n)/(199-s)`. Errore relativo massimo
// 0,5% (agli estremi n=0,s=100 e n=100,s=0) = ~1 cp su un'eval di 500.
// 🔴 BAKED ON il 7/08/2026, e **non per l'Elo**: misurato NEUTRO —
// **+1,04 +/- 4,90 su 6002 partite @15+0.15**, LLR -0,16. L'intervallo
// [-3,9 ; +5,9] non stabilisce un guadagno: stabilisce che non fa danno.
// Bakato su base di CORRETTEZZA. L'alternativa era tenere due formule diverse per
// lo stesso smorzamento rule50 dentro lo stesso motore, e chiunque ragioni in
// futuro sulla semantica dell'eval in TT partirebbe da una premessa falsa. Il
// difetto piu' redditizio della giornata (TTEvalNoDecay, +18 Elo) era esattamente
// questo: un commento che descriveva giusto e una costante che diceva altro.
// ⚠️ L'SPRT non e' stato portato a un bound: con elo0=0/elo1=5 e un effetto vero
// attorno a +1 l'LLR galleggia e servirebbero decine di migliaia di partite. La
// domanda a cui doveva rispondere — "fa danno?" — ha risposta a 6002.
// ⚠️ Il clamp a 100 e' obbligatorio anche nella variante nuova: senza, un fifty
// vicino a 199 azzererebbe il denominatore. In partita il contatore si ferma a 100.
// 🔴 "USARE nn_finalize AL POSTO DI redamp" — ESAMINATO IL 12/08/2026 E SCARTATO.
// L'idea era di far esistere la formula in un solo posto. Ma `nn_finalize` NON e' un
// rimpiazzo: oltre allo smorzamento applica anche `EvalScale` e un clamp alla banda
// TB, che `redamp` non fa — sostituirlo scalerebbe una seconda volta un valore gia'
// scalato. E anche il solo smorzamento non coincide sugli INTERI: `v - v*r/199`
// tronca in un punto diverso da `v*(199-r)/199`, quindi differiscono di un'unita' su
// meta' degli input. Non e' igiene a costo zero, e' un cambio di comportamento da
// misurare. Se un giorno si fa, va fatto con un toggle e un SPRT, non come refactor.
int g_rule50_formula = 1;
// CorrTBGuard — BAKATO A 1 il 12/08/2026. Corregge la guardia della banda Syzygy in
// `td_corr_update`: vedi il commento esteso al sito d'uso, ~:6955.
// ⚠️ SENZA tablebase e' un no-op ESATTO (nessuno score cade in [29873, 29935] se non
// viene da Syzygy), quindi il bench resta 252074 e i nostri test storici sono intatti.
// MISURA DI FREQUENZA (sonda con contatore, zero partite): nei finali fino a UN TERZO
// degli update di correction history porta uno score TB — 31,6% / 23,3% / 33,7% su tre
// finali; zero sulle stesse posizioni senza tablebase.
// SPRT con tablebase su entrambi i lati:
//   15+0.15, con aggiudicazione   3596 partite   -1,06 +/- 5,43
//    8+0.08, SENZA aggiudicazione 5374 partite   +1,10 +/- 4,61  (LLR -0,06)
// (il secondo perche' il draw adjudication dalla mossa 40 chiude le partite proprio nei
//  finali bilanciati, cioe' poteva aver rimosso il regime in esame invece di misurarlo)
// ⇒ ~9000 partite, nullo. **Preso sulla CORRETTEZZA, non sull'Elo**, come Rule50Formula.
// 🔑 L'argomento che regge da solo e' l'OVERFLOW, non la semantica. Coi default reali:
//     lim   = g_corr_cap(48) * CORR_GRAIN(256)                    = 12.288
//     bonus = diff(~29.900) * 256 * w(16) / g_corr_lr_div(303)    = 404.193
//     decay = cv * abs_bonus / lim  ->  12.288 * 404.193 = 4.966.723.584
//   contro INT32_MAX = 2.147.483.647. Overflow con segno = UB, non wrap benigno: clang
//   e' libero di assumere che non accada. E accade ESATTAMENTE nel caso tablebase, con
//   il bucket vicino al cap.
// Semanticamente: un verdetto Syzygy non e' una valutazione, e' una certezza. Farlo
// entrare in una tabella che impara l'ERRORE DI EVAL e' un errore di categoria — satura
// il bucket a +/-CorrCap e ci resta, e la pawn key a 14 bit lo propaga per collisione a
// posizioni che con le tablebase non c'entrano.
int g_corr_tb_guard = 1;
static inline int tt_eval_undamp(int v, int fifty) {
  if (v == tt_eval_none)
    return v;
  if (fifty > 100)
    fifty = 100;
  return g_rule50_formula ? v * 199 / (199 - fifty) : v * 214 / (200 - fifty);
}
static inline int tt_eval_redamp(int v, int fifty) {
  if (v == tt_eval_none)
    return v;
  if (fifty > 100)
    fifty = 100;
  return g_rule50_formula ? v * (199 - fifty) / 199 : v * (200 - fifty) / 214;
}

// TTEvalNoDecay (spin 0/1, default 0 = comportamento attuale).
// undamp e redamp sono l'una l'inversa dell'altra sui REALI, ma su interi
// TRONCANO ENTRAMBE verso zero: il round-trip perde modulo, sempre nella stessa
// direzione. E il valore ri-derivato non viene buttato, viene RISCRITTO nella
// stessa entry (tt.h:423, e anche nel ramo che conserva l'entry profonda,
// tt.h:398) -> l'output di un ciclo e' l'input del successivo, la contrazione
// COMPONE.
// 🔴 QUANTIFICATO (simulazione dell'aritmetica intera): 1,047 unita' perse per
// rivisita a fifty 0, 1,280 a fifty 50, 1,570 a fifty 100. Una unita' = 0,425 cp
// (EvalScale 60), un pedone ~235 unita', RFPMargin = 53 unita'/ply.
// ⚠️ MA a fifty COSTANTE l'attrattore NON e' zero: e' il multiplo di 107
// sottostante. Un'eval di 500 perde al piu' 107 unita' (~45 cp) e si FERMA. Va a
// zero solo sotto 107 unita', o quando il fifty cambia fra le rivisite
// (trasposizioni: la chiave TT non contiene il fifty). Quindi il difetto e' reale
// ma piu' piccolo di quanto sembri a prima vista: stima onesta +2..+6 Elo.
// 🔴 BAKED ON il 6/08/2026, e la stima qui sopra era SBAGLIATA di un fattore tre:
// gate SPRT SUPERATO con **+18,06 +/- 8,07 Elo, LOS 100%, LLR 2,95 (bound 2,94),
// 2618 partite @20+0.2**, baseline CorrMaterial=false. E' il guadagno singolo piu'
// grosso della campagna di ricerca 7.0.
// 🔑 Perche' la stima era bassa: contava la perdita per rivisita e l'attrattore a
// fifty costante, cioe' il caso statico. Il caso che paga e' quello DINAMICO — le
// trasposizioni, dove il fifty cambia fra le rivisite e l'attrattore si sposta,
// quindi la contrazione non si ferma mai. Non e' stato quantificato perche' non lo
// si vede in una simulazione dell'aritmetica: si vede solo giocando.
// 🔑 E dice quale classe di fix paga: TTEvalNoDecay corregge un errore che si
// COMPONE e non apre ricerca nuova. Gli altri due fix della stessa coda —
// QSCapRecap e BadCapSkipAfter — AGGIUNGONO sottoalberi, e hanno perso entrambi
// (-3,45 e -16,45). Tre su tre coerenti con questa distinzione.
static int g_tt_eval_no_decay = 1;

// Valore da mettere in TT allo store. Le QUATTRO guardie replicano ESATTAMENTE
// la condizione sotto cui raw_eval e' stato derivato da tt_eval; togliendone una
// il toggle cambierebbe ANCHE altro, e non sapremmo cosa abbiamo misurato:
//  - raw_eval != none : il ramo lazy-eval in scacco non calcola nessuna eval e
//    passa `none` apposta, cosi' store_tt CONSERVA l'ext esistente (tt.h:392).
//  - g_tt_static_eval : se OFF, raw viene da una td_evaluate() FRESCA mentre
//    tt_eval e' il valore vecchio -> riscriverlo congelerebbe un'eval stantia.
//  - tt_eval != none  : non abbiamo letto niente dalla TT.
//  - flag != none     : le entry eval-only di EvalTTWrite hanno formato
//    UNADJUSTED (si leggono con nn_finalize, non con redamp): scriverle in
//    un'entry reale sarebbe corruzione di formato.
static inline int tt_store_eval(int raw_eval, int tt_eval, int tt_flag,
                                int fifty) {
  if (g_tt_eval_no_decay && g_tt_static_eval && raw_eval != tt_eval_none &&
      tt_eval != tt_eval_none && tt_flag != hash_flag_none)
    return tt_eval;
  return tt_eval_undamp(raw_eval, fifty);
}
// 5.1 LOSSLESS (EvalTTWrite): l'entry eval-only memorizza l'eval SMORZATA + il
// fifty di store. La si usa SOLO se fifty_store == fifty_now -> il valore e'
// ESATTAMENTE quello che td_evaluate darebbe (stessa posizione, stesso fifty,
// deterministico) = zero differenza = tree-neutro. fifty diverso -> trattata
// come miss (eval fresca): mai un valore approssimato (niente bloat).

// P2.2 (UCI "FastRepScan", default ON): td_is_repetition scandisce solo le
// ultime min(fifty, plies_from_null) entry invece dell'INTERA storia. fifty e'
// un bound esatto (una mossa irreversibile cambia il board per sempre: nessuna
// ricorrenza possibile oltre); plies_from_null esclude i segmenti oltre una
// null move (le null pushano entry "virtuali" senza toccare fifty — il vecchio
// scan completo le attraversava). Semantica = SF; node-count puo' cambiare di
// un soffio -> SPRT.
static bool g_fast_rep_scan = true;
void set_fast_rep_scan(bool v) { g_fast_rep_scan = v; }


// ---- Toggle DA CO-TUNE per il mega-SPSA 4.0 (2026-06-12)
// --------------------- Strutture SF implementate dietro toggle default OFF =
// byte-identico. NON vanno A/B-ate da sole (lezione two-basin: bolt-on con
// costanti altrui = negativo, es. PawnHistory −18 bolt-on → +Elo dopo co-tune):
// si accendono nel co-tune con le loro costanti nel vettore SPSA, insieme a
// PriorBonus/LowPly.


// P1.6 (UCI "NMPVerif"): robustezza null-move — (a) niente due null
// consecutive; (b) a depth >= NMPVerifDepth un fail-high della null va
// CONFERMATO da una search reale ridotta (anti-zugzwang; SF: nmpMinPly). Spin
// NMPVerifDepth.
static bool g_nmp_verif = true;
void set_nmp_verif(bool v) { g_nmp_verif = v; }
int g_nmp_verif_depth = 3; // [PR1 1->3] spin "NMPVerifDepth" (SPSA) // spin "NMPVerifDepth" (SPSA)

// P1.7 (UCI "LMPImproving"): move-count pruning SF-style (base + d^2*quad/100)
// / (2 - improving), SENZA il cap depth<=8 della lmp_table (oggi oltre d8
// NESSUN move-count pruning = parte del fattore-nodi vs SF). Spins
// LMPBase/LMPQuad, scala comune LMPScale. Two-basin: costanti da co-tunare, non
// copiate.
static bool g_lmp_improving = true;
void set_lmp_improving(bool v) { g_lmp_improving = v; }
int g_lmp_base = 16;  // spin "LMPBase"
int g_lmp_quad = 145; // [PR1 141->145] [M20 138->141] spin "LMPQuad" (/100: 100 = d^2 pieno)


// N1 (UCI "EvalCacheUndamp", default ON): eval-cache con chiave senza fifty e
// valore undamped (vedi td_evaluate). Piu' hit nei finali; OFF = legacy.
static bool g_evalcache_undamp = true;
// EvalCacheOptTag (2026-08-02) — default OFF = byte-identico.
// L'optimism entra DENTRO il valore ritornato da nn_scale (nnue_bridge.cpp:153) e
// g_optimism viene riscritto dal thread 0 a OGNI iterazione ID (thread_search, search/13_iterdeep.inc).
// La cache e' pero' indicizzata solo su hash_key: un hit puo' quindi servire una eval
// calcolata con un contempt diverso da quello corrente. Non e' rumore innocuo — a score
// ±2000 l'optimism vale ~68 unita' sulla static eval, piu' di un passo di RFPMargin (53),
// e static_eval alimenta RFP, futility, razoring, improving e la correction history.
// Con il tag, un hit con optimism diverso diventa un miss e si ricalcola.
// ⚠️ E' un tradeoff: meno hit (piu' forward NNUE, che e' il 52% del wall) contro eval
// corrette. Per questo e' un toggle e non una correzione secca.
void set_evalcache_undamp(bool v) { g_evalcache_undamp = v; }

// N2 (UCI "ProbCutTT", default ON): il fail-high di ProbCut viene SALVATO in TT
// (mossa, score, depth-3, bound beta, eval del nodo) come SF -> le rivisite
// della stessa posizione tagliano dal probe senza rifare qsearch+verifica.
static bool g_probcut_tt = true;
void set_probcut_tt(bool v) { g_probcut_tt = v; }

// P2.3 (UCI "EvasionGen", default ON): sotto scacco genera solo le
// pseudo-legali UTILI (re + catture dello scaccante + blocchi sul raggio;
// doppio scacco: solo re) intersecando le maschere ESISTENTI -> meno make
// falliti. VERIFICATO (2026-06-11, harness simmetrico order-sensitive, 0
// mismatch su pos in-scacco d6 incl. captures-only): i flussi di mosse LEGALI
// (set E ordine) sono identici on/off a ogni chiamata. Residuo ±0.3% di
// node-count da effetti di 2° ordine non-semantici (bookkeeping) -> NON
// node-identical stretto: si valida con l'SPRT del bundle come gli altri
// toggle.
static bool g_evasion_gen = true;
void set_evasion_gen(bool v) { g_evasion_gen = v; }

// Eval-off DIAGNOSTIC toggle (UCI option "EvalOff", default false). Replaces
// the NNUE forward in td_evaluate() with a trivial material count, so an NPS
// test can measure how much per-node time is spent in evaluation (NNUE forward)
// vs the rest (movegen / make-unmake / mirror / search overhead). NOT for play.
static bool g_eval_off = false;
void set_eval_off(bool v) { g_eval_off = v; }

// Static-eval cache on/off (UCI option "EvalCache"). Default on; the toggle
// lets us A/B the cache (off vs on) back-to-back in a single build, immune to
// run-to-run NPS drift between separate builds.
static bool g_eval_cache = true;
void set_eval_cache(bool v) { g_eval_cache = v; }

// FinnyTables (NNUE accumulator refresh cache) on/off (UCI option
// "FinnyTables"). Default OFF => byte-identical to the pre-finny engine.
// Forwards to the bridge (the cache lives per-thread in the SfPos mirror). Pure
// NPS lever: eval is bit-identical either way, so validate with an interleaved
// A/B NPS test.
void set_finny(bool v) { nn_set_finny(v ? 1 : 0); }

// "Improving" heuristic on/off (UCI option "Improving"). Default on; off
// reproduces the exact pre-heuristic search for a clean A/B from one build.
static bool g_improving = true;
void set_improving(bool v) { g_improving = v; }

// Node-based time management on/off (UCI option "NodeTM"). Default on; off
// reproduces the pure best-move-stability time logic for a clean A/B.
static bool g_node_tm = true;
void set_node_tm(bool v) { g_node_tm = v; }

// Advanced singular extensions on/off (UCI option "SingularExt"). Default on;
// off reproduces the plain single-ply singular extension for a clean A/B.
static bool g_singular_ext = true;
void set_singular_ext(bool v) { g_singular_ext = v; }

// Correction history on/off (UCI option "CorrHist"). Default ON (PROVISIONAL):
// the cap=32 / lr-div=512 version leaned +Elo (ultrabullet 2+0.02: ~+5.5, LOS
// ~81%
// @1750 games, CI still touching 0). Ultrabullet UNDERSELLS a depth-dependent
// correction, so adopted provisionally pending a longer-TC (8+0.08) SPRT.
// Toggle off for a clean A/B; tunable via CorrCap / CorrLearnDiv.
static bool g_corr_hist = true;
void set_corr_hist(bool v) { g_corr_hist = v; }

// Multi-table correction history on/off (UCI option "CorrHistMulti"). Default
// OFF reproduces the pawn-only correction. When ON, two extra correction tables
// keyed on minor (N/B) and major (R/Q) material are summed with the pawn table
// before clamping — the net mis-evaluates material configurations too, not just
// pawns.
static bool g_corr_multi =
    true; // BAKED ON (2026-06-05): HM compound +6.2 LOS87.6% @1338
void set_corr_multi(bool v) { g_corr_multi = v; }
// CorrHistMajor (10/09/2026): isola il termine dei PEZZI MAGGIORI dentro CorrHistMulti.
// Stockfish 19 somma pedoni, minori, non-pedoni e continuazione: nessun termine sui
// maggiori. E CorrHistMulti fu bakato nello stesso pacchetto di ContHistMulti, sulla
// stessa prova (+6,2 LOS 87,6% su 1.338 partite) che per ContHistMulti si e' rivelata
// nulla. Default ON = identico a prima (firma bench invariata).
static bool g_corr_major = true;
void set_corr_major(bool v) { g_corr_major = v; }

// Continuation correction history on/off (UCI option "CorrHistCont"). Default
// OFF = byte-identical (the cont_corr_hist table is never read/written when
// off). When ON, adds the SF continuation-correction term — keyed by the last
// two moves INTO the node (path-dependent static-eval correction) — to the corr
// sum. Its contribution is co-tunable via CorrContWeight (lets a co-tune
// balance cont vs pawn/minor/major).
// BAKED ON (2026-07-22): SPRT +13.97 Elo (MathFix + CorrHistCont + Turbo_x2)
static bool g_corr_cont = true;
void set_corr_cont(bool v) { g_corr_cont = v; }
int g_corr_cont_weight =
    77;   // [MOE1 85->77, 30/09/2026] BAKED 6/08/2026 col blocco corrections (SPSA TC30 + SPRT +2,65) // /100 del contributo cont alla somma corr. Spin CorrContWeight.

int g_corr_np_weight = 100; // /100 sul contributo delle due tabelle non-pawn

int g_corr_material_weight = 67; // /100 sul contributo della tabella material


// PawnHistory (UCI "PawnHistory", default OFF = byte-identico). Termine di
// ordering per i quiet, pesato 2x come in SF, keyed sulla struttura pedonale.
// Tabella per-thread in ThreadData (SMP-safe). Indice ricalcolato in
// td_score_move dal board CORRENTE (durante lo scoring il board e' sempre
// quello del nodo -> niente staleness da ricorsione).
static bool g_pawn_hist =
    true; // BAKED #1 2026-06-07 (era false): co-tune neutro@8 / +3@20+0.08
void set_pawn_hist(bool v) { g_pawn_hist = v; }
int g_pawn_hist_weight =
    211; // [M20 187->211] [4.1 BAKE 126->139]   // [3.7 BAKE 195->83] peso pawn-history /100
         // (200 = 2.0x come SF). Spin PawnHistoryWeight (granularita' /100,
         // SPSA-friendly + permette frazioni <1).
// Peso della main (butterfly) history nello scoring quiet. SF la pesa 2x (come
// la pawn); noi storicamente 1x -> con pawn a 2x la pawn DOMINA la main =
// sbilanciato. Spin MainHistWeight per copiare il rapporto SF (main 2x, pawn
// 2x). Default 1 = byte-identico.
int g_mainhist_weight = 93; // [M20 93->93] [4.1 BAKE 122->168]   // [3.7 BAKE 209->131] /100
                            // (100 = 1.0x; SF usa 2.0x=200)
// Peso della continuation-history nello scoring quiet (ordering). Default 1 =
// invariato. Manopola del CO-TUNE: bilancia conthist vs main/pawn. NON tocca
// conthist in LMR/pruning.
int g_conthist_weight =
    144; // [M20 135->144] [4.1 BAKE 80->96]   // [3.7 BAKE 134->150] /100 (100 = 1.0x)
// Scala % della soglia LMP (late-move-pruning). Default 100 = invariato. <100
// pota prima (albero più stretto), >100 pota dopo. Co-tune: si ri-equilibra con
// l'ordering nuovo.
int g_lmp_scale = 38; // [PR1 35->38] [3.7 BAKE 93->116]
// Forward-decl: td_corr_index (pawn-only Zobrist bucket) e' definita piu'
// sotto, ma serve qui sopra in td_score_move per la pawn-key.
static inline int td_corr_index(ThreadData &td);
// Forward-decl: td_corr_value serve anche in td_quiescence (P0.4), definita
// dopo.
static inline int td_corr_value(ThreadData &td, int idx);
// Forward-decl: init tabelle cuckoo/between per l'upcoming-repetition (P1.2),
// chiamata da init_threads (Zobrist + magic gia' inizializzati a quel punto).
static void init_cuckoo();

// ProbCut on/off (UCI option "ProbCut"). Default ON: validated +Elo (SPRT
// @8+0.08, LOS ~83% / leaning +6, textbook technique). When on: at sufficient
// depth, if a capture's reduced verification search beats beta+margin, the
// full-depth search would almost surely fail high too, so we prune the node
// early.
static bool g_probcut = true;
void set_probcut(bool v) { g_probcut = v; }

// Continuation-history pruning/reduction on/off (UCI option "ContHistPrune").
// Default ON (PROVISIONAL): tested jointly with CorrHist (ultrabullet ~+5.5,
// LOS ~81%); adopted provisionally pending a longer-TC SPRT. When on: (1) the
// LMR reduction also factors in the continuation history of the move w.r.t. the
// previous move (not just butterfly history), and (2) late quiet moves whose
// combined (butterfly + continuation) history is strongly negative are pruned
// at low depth. Toggle off for a clean A/B; tunable via ContHistDiv /
// HistPruneMargin.
// ABLAZIONE A4 (audit 8.0, 26/09/2026): la conferma a TC lungo non era mai arrivata. ContHistPrune=false
// contro base, SPRT [-1.75, 0.25] a 10+0.1 Hash 64: +5,94 ± 3,98 Elo su 8.957 partite (zero escluso,
// LLR ~1,7; fermata e accettata dall'utente). Senza la potatura l'albero cresce del 14% al bench, ma il
// motore GUADAGNA. SPENTA di default; il codice resta. La conferma a TC lungo e' lo SPRT 8.0 contro 7.0.
static bool g_cont_hist_prune = false; // BAKED OFF 2026-09-26 (ablazione A4); era true (provvisorio)
void set_cont_hist_prune(bool v) { g_cont_hist_prune = v; }


// Staged MovePicker on/off (UCI option "MovePicker"). ADOPTED, default ON:
// validated +16.6 Elo (LOS 96.6% @900 games, 5+0.1, vs the legacy path) once
// the Phase-2 skip_quiets/skip_bad_caps were fixed + tested at a real TC (at
// hyperbullet it was ~0). When ON, moves are produced lazily by stage (TT ->
// good captures/promos -> killers -> counter -> quiets -> bad captures): an
// early cutoff never pays to generate/score the quiet moves, and LMP/futility
// skip the whole quiet stage. Set OFF to reproduce the old generate-all +
// pick-next ordering for an A/B.
static bool g_move_picker = true;
void set_move_picker(bool v) { g_move_picker = v; }

// DiverseSMP (#2): Lazy-SMP helper threads (id>0) search with a small
// per-thread LMR reduction bias, widening the ensemble of trees to cut
// redundant work at high thread counts. Thread 0 stays canonical. Default off
// (behaviour-preserving).
// **BAKED ON (2026-06-04, bake-on-trust):** wider-only diversity (bias<=0, no
// hijack) e' SEMPRE stato positivo (+3..+10, mai negativo) su ~1440 partite a
// 4t+8t. Feature SMP: inerte a 1 thread, agisce solo a multi-thread (=
// condizione gauntlet/torneo). Un [0,5] SPRT non puo' confermare un ~+3 (LLR a
// zonzo), ma il segno e' robusto. Toggle conservato per A/B (off = baseline).
// amount=1.
static bool g_diverse_smp = true;
void set_diverse_smp(bool v) { g_diverse_smp = v; }
static int g_diverse_smp_amount =
    1; // max |bias| in plies (SPSA-tunable: DiverseSMPAmount)
// ⭐ DiverseSMPFine (2026-07-20, default 0 = comportamento storico,
// byte-identico): bias helper in MILLESIMI di ply invece che in ply interi.
// Diagnosi del perche' DiverseSMP non ha mai reso: il nostro bias minimo e' 1
// ply = 1024 unita' LMRFine, mentre Reckless (fb19535) vince con 96/48 unita'
// (~0.05-0.1 ply) e BOCCIA gia' a 512/256. Il nostro "minimo" e' 2-4x la loro
// variante fallita: non e' una perturbazione, e' un helper menomato. Con fine>0
// il bias diventa -mag*fine unita' (wider-only, stessa crescita per coppie di
// id, cap mag=8). Da testare a Threads>=8; a 1 thread e' morto per costruzione
// (bias solo sugli helper). ✅ BAKATO A 96 (2026-07-20): SPRT SMPFINE(96) vs
// BASE(legacy ply-intero) a Threads=8, 16+0.16,
//    hash 256: +4.91 +/- 7.97 Elo (nElo +10.17), LOS 88.63%, 1700 partite, LLR
//    non conclusivo ma lean stabilmente positivo (mai negativo lungo tutto il
//    run). A 8 thread un SPRT locale non chiude in tempi ragionevoli -> bakato
//    sul lean, non su una chiusura formale. A Threads=1 e' byte-identico (il
//    bias vive solo sugli helper, id>0).
int g_diverse_smp_fine = 96;

// --- Search "bundle" #3 — OUTCOME (2026-06-03): tested all-ON (-58 Elo), then
// isolated.
//  * MultiCut   — singular multi-cut, CONSERVATIVE gate (singular_beta >=
//  beta): the only
//                 winner -> +10.4 ±11.2 Elo LOS 96.6% @1100 (1t, 8+0.08). BAKED
//                 default ON.
//  * TripleExt  — +3-ply singular: -75 Elo (tree blow-up). DEAD, kept
//  default-off dormant.
//  * LMREnrich  — extra LMR on ttCapture/cut-node: -25 Elo (over-reduction).
//  DEAD, dormant.
//  * MultiCutAggr (SF gate s>=beta): -13 Elo (half-depth fail-high too weak).
//  REMOVED.
// (TripleExt −75, LMREnrich −25, DeeperShallower: misurati NEGATIVI/morti —
//  RIMOSSI nella pulizia 2026-06-11, vivono nella storia git.)
static bool g_multicut =
    true; // BAKED ON: conservative singular multi-cut (+10.4 Elo @1100)
void set_multicut(bool v) { g_multicut = v; }
int g_multicut_ttmalus_scale =
    100; // % di td_stat_bonus(depth), come le altre scale malus/bonus
// ⭐ MulticutCorr (16/08/2026, port SF 218c74ec — l'idea e' di PlentyChess). Quando
// il multicut scatta, la ricerca di esclusione ha appena stabilito che il valore vero
// del nodo sta SOPRA la static eval: e' esattamente il campione che la correction
// history esiste per imparare, e lo stavamo buttando via.
// 🔑 E' l'UNICA patch della finestra 01/08-16/08 che SF ha passato a soglie da
// GUADAGNO invece che da non-regressione: STC <0.00,2.00> su 151.072 partite e LTC
// <0.50,2.50> su 109.866. Le altre (c5aef2bf, fa8b6add, c85637b3, de948f0f) sono
// tutte <-1.75,0.25>, cioe' valgono ~0 Elo per costruzione.
// ⚠️ Il nostro gate di multicut NON e' quello di SF: loro sparano sul VALORE
// (`value >= beta`), noi sul BOUND (`singular_beta >= beta`), perche' la loro forma
// aggressiva qui aveva misurato −13 Elo (vedi il bundle #3 a :678). L'evidenza da
// imparare resta pero' `s` — il risultato della ricerca di esclusione, che e'
// esattamente cio' che SF chiama `value` — e non `singular_beta`, che e' solo il
// bound conservativo che restituiamo.
// Default OFF = byte-identico.
static bool g_multicut_corr = false;
// Peso dell'update, in % di `singular_depth`. L'update ordinario (td_corr_update)
// pesa `min(depth,16)`; qui la base e' singular_depth = (depth-1)/2, cioe' circa
// META', perche' un fail-high a finestra nulla e profondita' ridotta e' evidenza piu'
// debole di una ricerca piena. 125 porta il rapporto a ~0.6x dell'update normale, che
// e' quello che usa SF (177/1024 su singularDepth contro il loro update ordinario).
// ⚠️ E' il candidato naturale allo SPSA una volta che il toggle abbia passato un SPRT.
int g_multicut_corr_scale = 125;

// ttPv (UCI spin "TTPvAmount", 0..2, default 0=OFF). SF: un bit della TT (bit
// 63 del data, era libero) ricorda se un nodo è/è stato PV. I nodi non-PV che
// la TT marca come ex-PV vengono ridotti MENO in LMR (reduction -= amount),
// perché un nodo già stato PV merita più attenzione. Il flag viene propagato
// negli store quando amount>0. REPARAMETRIZZATO (2026-06-24, lesson #14): da
// toggle bool a intero auto-spegnibile — 0=off (byte-identico), 1=−1ply (ex
// comportamento), 2=−2ply. Così il co-tune può dialarlo, e se resta dannoso
// (isolato era −2.6) lo SPSA lo riporta a 0 da solo.
int g_ttpv_amount = 1;
