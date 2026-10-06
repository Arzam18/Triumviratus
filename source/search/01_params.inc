// search/01_params.inc -- Parametri della ricerca e globali condivise con le altre unita'.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp.
//
// BAKE RW1 (04/10/2026): 49 valori sono la media delle iterazioni 4481-5481 dello SPSA "RW1" (51 leve, 20+0.2,
// Tuning_SPSA/spsa_prune/vettore_rw1_it5481.json), giocato dal nostro motore con Consilium; PassedPushRed (nostra)
// entra accesa a 1125. SPRT di gate contro i valori di partenza a 10+0.1: +9,67 +- 9,38 (LLR 0,61, fermato e
// accettato dall'utente). Bench 141196 (prima 172833). Fuori dallo SPSA, quindi ancora ai valori di partenza: la
// gestione del tempo, le soglie di profondita' e LmrLogMul.
// BAKE LDSE (05/10/2026): LdseMargin 0 -> 80, LdseMode 0 -> 1 (estensione della mossa hash a profondita' bassa, solo
// nei nodi cut; 13_search.inc, move_loop). SPRT a 10+0.1 sul binario t80_d1005, libro UHO, 40/30 partite per
// socket: +4,52 +- 3,34 su 11.604 partite (LLR 2,04 su [-2,94, 2,94], nElo [0, 3]); socket 0 (4 banchi di RAM, piu'
// veloce e piu' profondo) +6,8 +- 4,4, socket 1 (2 banchi) +1,5 +- 5,1: sembra rendere di piu' con piu' profondita'.
// Fermato e bakato su decisione dell'utente prima delle 20.000 partite. Bench 498873 (prima 141196; con
// LdseMargin=0 torna 141196): l'estensione vicino all'orizzonte allarga molto gli alberi a profondita' fissa.
// Sui finali (libro endgame_12_18, 10+0.1) Ldse spenta contro accesa -0,88 +- 4,05 su 2.786 partite: non dannosa.
// BAKE GUARDIA (05/10/2026): LdseMax 0 -> 1, al massimo un'estensione Ldse per linea (NodeFrame::ldse_path). Nei
// finali di torre l'estensione si ripeteva lungo la linea (albero x5-x16 a profondita' fissa). SPRT contro Ldse senza
// tetto a 15+0.15, UHO: +6,25 +- 7,89 su 2.112 partite (LLR 0,54; socket 0 +7,4, socket 1 +4,6), fermato e bakato
// su decisione dell'utente. Bench 308883 (con LdseMax=0 torna 498873).
// BAKE SORPRESA (05/10/2026): TmSurpriseScale 1000 -> 1200 (tempo della mossa x1,2 quando l'avversario non ha giocato
// la risposta della nostra linea principale; 14_deepen.inc). SPRT a 40+0.4, UHO, seme 7200: +6,23 +- 6,75 su 2.174
// partite (LLR 0,65, LOS ~96%; socket 0 +6,2, socket 1 +6,3), fermato e bakato su decisione dell'utente. Dai PGN: sulle
// mosse dopo una sorpresa (~26%) +0,35-0,4 ply. A 16+0.16 era neutra (+0,7): dipende dal TC (vedi il parametro).
// Il bench (profondita' fissa) non usa il tempo: resta 308883.
// BAKE HASHQSDEPTH (06/10/2026): HashQsDepth 0 -> 1 (la quiescenza scrive e legge profondita' 0 in hash, non -1;
// 12_quiesce.inc). SPRT a 10+0.1, UHO, 40/30: +3,38 +- 2,58 su 20.162 partite (LLR 2,39, zero escluso); socket 0
// +1,0 +- 3,5, socket 1 (~0,4 ply piu' profondo) +6,6 +- 3,9. Bakato su decisione dell'utente. Bench 222624 (con
// HashQsDepth=0 torna 308883).
// Riscrittura del 04/10/2026: i margini della ricerca sono espressi in unita' proprie (pedone ~ 356); la valutazione
// della rete viene portata a queste unita' da EvalUnits (09_history.inc), cosi' i margini non dipendono dalla scala
// della rete. Ogni parametro e' un'opzione UCI nelle build di sviluppo e una
// costante nella build di release (TRIUMV_RELEASE), dove il compilatore la piega nel codice.

// ---------------------------------------------------------------------------
// Tabella dei parametri: nome, default, minimo, massimo.
// ---------------------------------------------------------------------------
#define TRIUMV_SEARCH_PARAMS(P)                                                                            \
  /* valutazione */                                                                                       \
  P(EvalUnits, 1347, 900, 1800)          /* /1024: unita' nostre -> unita' dei margini (pedone ~ 356) */  \
  P(OptStrength, 182, 0, 600)            /* optimism = forza * s / (|s| + div), in unita' della rete */   \
  P(OptDiv, 288, 1, 600)                                                                                  \
  /* finestre di aspirazione */                                                                           \
  P(AspBase, 5, 1, 40)                                                                                    \
  P(AspMeanSqDiv, 10193, 1000, 40000)                                                                     \
  P(AspGrow, 48, 10, 128)                /* /128 */                                                       \
  /* history: bonus e malus */                                                                            \
  P(BonusMul, 139, 50, 300)                                                                               \
  P(BonusSub, 81, 0, 300)                                                                                 \
  P(BonusMax, 1565, 500, 3000)                                                                            \
  P(BonusTTMove, 371, 0, 1000)                                                                            \
  P(BonusPrevStatDiv, 28, 4, 200)                                                                         \
  P(MalusMul, 911, 300, 2000)                                                                             \
  P(MalusSub, 235, 0, 800)                                                                                \
  P(MalusMax, 2186, 800, 5000)                                                                            \
  P(QuietBonusScale, 798, 400, 1600)     /* /1024 */                                                      \
  P(QuietMalusScale, 1031, 400, 2000)                                                                     \
  P(QuietMalusDecay, 921, 600, 1024)                                                                      \
  P(CaptBonusScale, 1515, 500, 2500)                                                                      \
  P(CaptMalusScale, 1370, 500, 2500)                                                                      \
  P(PrevContMalus, 713, 0, 1500)                                                                          \
  P(LowPlyScale, 712, 0, 1500)                                                                            \
  P(ContUpdScale, 784, 300, 1500)                                                                         \
  P(PawnBonusScale, 1104, 300, 2000)                                                                      \
  P(PawnMalusScale, 459, 100, 1500)                                                                       \
  P(EvalDiffMul, 11, 0, 40)               /* history della mossa precedente dalla differenza di eval */   \
  P(EvalDiffPawnMul, 13, 0, 40)                                                                           \
  /* ordinamento */                                                                                       \
  P(GoodQuietLimit, -14491, -30000, 0)                                                                    \
  P(QuietSortDepthMul, 3487, 1000, 8000)                                                                  \
  P(CheckOrderBonus, 15142, 0, 40000)                                                                     \
  P(ThreatOrderMul, 20, 0, 60)                                                                            \
  P(GoodCaptSeeDiv, 18, 4, 64)                                                                            \
  /* potatura prima del ciclo mosse */                                                                    \
  P(RazorMul, 442, 100, 1200)                                                                             \
  P(RfpMaxDepth, 19, 4, 30)                                                                               \
  P(RfpMultBase, 50, 10, 120)                                                                             \
  P(RfpMultDepth, 4, 0, 16)                                                                               \
  P(RfpMultCap, 85, 30, 200)                                                                              \
  P(RfpNoTTHit, 20, 0, 60)                                                                                \
  P(RfpImproving, 2852, 0, 5000)                                                                          \
  P(RfpOppWorse, 335, 0, 1500)                                                                            \
  P(RfpCorrDiv, 198435, 50000, 800000)                                                                    \
  P(RfpBetaWeight, 712, 0, 1024)                                                                          \
  P(RfpOppCapture, 0, 0, 1024)           /* nostra (05/10/2026): quota (/1024) dello sconto "improving" della */ \
  P(NmpOppCapture, 0, 0, 1)              /* RFP persa se l'avversario ha una cattura facile; 1 = niente nulla */ \
  P(NmpDepthMul, 14, 0, 40)                                                                               \
  P(NmpImproving, 47, 0, 150)                                                                             \
  P(NmpBase, 314, 0, 800)                                                                                 \
  P(NmpRedBase, 7, 3, 12)                                                                                 \
  P(NmpEvalDiv, 235, 64, 1024)                                                                            \
  P(NmpVerifyDepth, 16, 6, 30)                                                                            \
  P(ProbCutMargin, 231, 80, 500)                                                                          \
  P(ProbCutImproving, 64, 0, 200)                                                                         \
  P(ProbCutTTMargin, 400, 150, 900)                                                                       \
  /* potatura nel ciclo mosse */                                                                          \
  P(CaptFutBase, 177, 50, 600)                                                                            \
  P(CaptFutDepth, 279, 80, 500)                                                                           \
  P(CaptFutHist, 134, 0, 400)            /* /1024 */                                                      \
  P(CaptSeeDepth, 186, 50, 400)                                                                           \
  P(CaptSeeHist, 34, 0, 120)             /* /1024 */                                                      \
  P(QuietHistPrune, 3910, 1000, 10000)                                                                    \
  P(QuietMainHistMul, 69, 0, 200)        /* /32 */                                                        \
  P(QuietFutDepth, 122, 40, 300)                                                                          \
  P(QuietFutAlpha, 90, 0, 300)                                                                            \
  P(QuietFutBase, 166, 0, 400)                                                                            \
  P(QuietSeeMul, 25, 5, 80)                                                                               \
  /* estensioni */                                                                                        \
  P(SingMarginBase, 73, 20, 150)                                                                          \
  P(SingMarginTtPv, 66, 0, 200)                                                                           \
  P(SingDoubleBase, -2, -100, 100)                                                                        \
  P(SingDoublePv, 217, 0, 500)                                                                            \
  P(SingDoubleQuiet, 152, 0, 400)                                                                         \
  P(SingTripleBase, 67, 0, 300)                                                                           \
  P(SingTriplePv, 279, 0, 600)                                                                            \
  P(SingTripleQuiet, 188, 0, 500)                                                                         \
  P(SingTripleTtPv, 81, 0, 300)                                                                           \
  P(LdseMargin, 80, 0, 400)              /* BAKE LDSE 05/10/2026 (era 0, vedi in testa): a profondita' <= */ \
  P(LdseDepth, 7, 1, 12)                 /* LdseDepth la mossa hash con limite inferiore si estende se    */ \
  P(LdseMode, 1, 0, 7)                   /* l'eval sta sotto alpha - m. Mode (era 0) bit: 1 solo nodi cut, */ \
                                         /* 2 solo limite inferiore, 4 non da quiescenza                 */ \
  P(LdseMax, 1, 0, 8)                    /* BAKE 05/10/2026 (era 0, vedi in testa): tetto alle estensioni */ \
                                         /* Ldse lungo una linea (0 = nessun tetto)                      */ \
  /* riduzioni (1/1024 di ply) */                                                                         \
  P(LmrLogMul, 2872, 1500, 4000)         /* /128 */                                                       \
  P(LmrDeltaMul, 577, 0, 1500)                                                                            \
  P(LmrNotImproving, 197, 0, 600)        /* /512 */                                                       \
  P(LmrBase, 998, 0, 2500)                                                                                \
  P(LmrTtPvAdd, 929, 0, 2500)                                                                             \
  P(LmrTtPvSub, 3227, 0, 6000)                                                                            \
  P(LmrTtPvPv, 1004, 0, 3000)                                                                             \
  P(LmrTtPvAlpha, 885, 0, 3000)                                                                           \
  P(LmrTtPvDepth, 816, 0, 3000)                                                                           \
  P(LmrTtPvCut, 940, 0, 3000)                                                                             \
  P(LmrOffset, 697, -1500, 2500)                                                                          \
  P(LmrMoveCount, 60, 0, 300)                                                                             \
  P(LmrCorrDiv, 26310, 5000, 100000)                                                                      \
  P(LmrCutNode, 4093, 1000, 7000)                                                                         \
  P(LmrCutNoTT, 933, 0, 3000)                                                                             \
  P(LmrTTCapture, 1079, 0, 3000)                                                                          \
  P(LmrCutoffBase, 264, 0, 1500)                                                                          \
  P(LmrCutoffMany, 1095, 0, 3000)                                                                         \
  P(LmrCutoffAll, 1138, 0, 3000)                                                                          \
  P(LmrTTMove, 2082, 0, 5000)                                                                             \
  P(LmrStatDiv, 377, 100, 1500)          /* /4096 */                                                      \
  P(LmrAllNode, 276, 0, 800)                                                                              \
  P(LmrNoTTFull, 1127, 0, 3000)                                                                           \
  P(DeeperMargin, 50, 0, 200)                                                                             \
  P(ShallowerMargin, 8, 0, 60)                                                                            \
  /* quiescenza */                                                                                        \
  P(QsFutMargin, 285, 100, 600)                                                                           \
  P(QsSeeMargin, 80, 0, 300)                                                                              \
  P(QsQuietHash, 0, 0, 1)                /* nostra (05/10/2026): nei nodi non-PV della quiescenza la mossa */ \
                                         /* hash quieta con limite inferiore o esatto si cerca come tattica */ \
  /* correction history */                                                                                \
  P(CorrPawnW, 17291, 0, 40000)                                                                           \
  P(CorrMinorW, 10827, 0, 40000)                                                                          \
  P(CorrNonPawnW, 11613, 0, 40000)                                                                        \
  P(CorrContW, 8022, 0, 30000)                                                                            \
  P(CorrMinorUpd, 150, 0, 400)           /* /128 */                                                       \
  P(CorrNonPawnUpd, 186, 0, 400)                                                                          \
  /* tecniche nostre sopra la ricerca nuova (04/10/2026), spente = albero invariato */                    \
  P(KingShield, 0, 0, 40000)             /* malus d'ordinamento al pedone che lascia lo scudo del re */   \
  P(KingShieldNpm, 3600, 0, 8000)        /* ...solo se l'avversario ha almeno questo materiale non pedoni */ \
  P(CorrMajorW, 0, 0, 40000)             /* correzione per i pezzi maggiori (torri e donne) */            \
  P(CorrMajorUpd, 150, 0, 400)                                                                            \
  P(HashDamp, 0, 0, 200)                 /* taglio da limite inferiore spinto verso beta (decimi) */      \
  P(HashFiftyStep, 0, 0, 50)             /* nostra (05/10/2026): la chiave hash porta la fascia del contatore */ \
  P(HashFiftyFrom, 40, 0, 100)           /* delle 50 mosse, una ogni Step semimosse sopra From (0 = spenta) */ \
  P(PassedPushRed, 1125, 0, 4096)           /* finali: meno riduzione e niente potatura per la spinta di un */ \
  P(PassedPushNpm, 5133, 0, 12000)       /* pedone passato in 6a/7a, sotto questo materiale non pedoni   */ \
  /* Idee nostre del 04/10/2026 (docs/audit_8.0/M_STUDIO_IDEE_RICERCA.md), tutte SPENTE di default:         \
     a 0 l'albero e' identico (bench 141196) e nella release il codice sparisce. Si accendono una per volta  \
     negli SPRT del piano (STATO_PROGETTO.md 4.0quinquies). Tolte il 05/10/2026 perche' negative (§3 dello     \
     stato): 1 Disagree* (entrambi i segni), 4 PhaseEdgeCapMargin e PhaseEdgeImproving, 5 PassedReplyRed,       \
     6 NmpVerifyDepthEnd/Npm, 10 LateBranch, e HashNearMiss. */                                               \
  P(DeepKnee, 16, 4, 40)                 /* idea 2 (nostra, LTC): da questa profondita' di radice...     */ \
  P(DeepSpan, 12, 1, 40)                 /* ...in questi ply il peso dei termini Deep* sale da 0 a 1      */ \
  P(DeepRfp, 0, -200, 200)               /* scostamenti "profondi" (0 = spenti), li cerca un nostro SPSA LTC */ \
  P(DeepFut, 0, -200, 200)                                                                                \
  P(DeepLmr, 0, -2048, 2048)             /* /1024 di ply */                                               \
  P(DeepNmp, 0, -2048, 2048)             /* /1024 di ply */                                               \
  P(DeepSing, 0, -60, 60)                                                                                 \
  P(Contempt, 0, 0, 200)                 /* idea 3: patta = -c per il colore di radice (Elo di lista) */  \
  P(PhaseEdgeNoFut, 0, 0, 1)             /* idea 4: la cattura che cambia esperto della rete non si pota e non si riduce di piu' */ \
  P(QsPasserPush, 0, 0, 1)               /* idea 7: in quiescenza la spinta in 7a di un passato e' tattica */ \
  P(CorrPhase, 0, 0, 1)                  /* idea 9: correzione dei pedoni/minori separata per esperto */  \
  P(CorrFiftyStep, 0, 0, 50)             /* nostra (05/10/2026): correzione di pedoni e non-pedoni separata */ \
                                         /* per fascia del contatore delle 50 mosse (Step semimosse, 0 = spenta) */ \
  P(TransitionExt, 0, 0, 1)              /* idea 13: la cattura che porta un lato a soli pedoni si estende */ \
  /* Audit del nucleo (05/10/2026): gli adattatori intorno alla ricerca (hash, SEE, mossa nulla, valutazione) rivisti \
     uno per uno, ognuno con una forma alternativa. Spenti = comportamento di prima (albero identico); ognuno va      \
     provato con uno SPRT. */                                                                                       \
  P(TTMoveRefresh, 0, 0, 1)              /* 1: la mossa nuova entra anche quando si conserva l'entry piu' profonda */ \
  P(SeePinned, 0, 0, 1)                  /* 1: nella SEE i pezzi inchiodati non ricatturano finche' c'e' l'inchiodatore */ \
  P(HashQsDepth, 1, 0, 1)                /* BAKE 06/10/2026 (era 0, vedi in testa): 1 = la quiescenza     */ \
                                         /* scrive e legge profondita' 0 in hash, non -1                 */ \
  P(NullFiftyKeep, 0, 0, 1)              /* 1: la mossa nulla non incrementa il contatore delle 50 mosse */ \
  P(EvalRefadeBridge, 0, 0, 1)           /* 1: la rilettura smorzata usa la formula del ponte (v - v*f/199) */ \
  /* Rampa "tempo lungo" (05/10/2026): le leve di selettivita' passano dal valore RW1 (tarato a 20+0.2, radice ~15)   \
     al valore dello SPSA LTC1 (40+0.4, radice ~17) secondo la profondita' di radice: peso 0 fino a LtKnee, 1 da       \
     LtKnee + LtSpan in su, senza estrapolare oltre. Gli Lt sono scostamenti dal valore base: a 0 albero identico, e   \
     nella release il codice sparisce. */                                                                     \
  P(LtKnee, 15, 4, 40)                                                                                     \
  P(LtSpan, 2, 1, 20)                                                                                      \
  P(LmrLogMulLt, 0, -1000, 1000)                                                                           \
  P(RfpMaxDepthLt, 0, -10, 10)                                                                             \
  P(NmpVerifyDepthLt, 0, -10, 10)                                                                          \
  P(LmrNotImprovingLt, 0, -300, 300)                                                                       \
  P(LmrOffsetLt, 0, -1500, 1500)                                                                           \
  P(LmrAllNodeLt, 0, -400, 400)                                                                            \
  P(LmrNoTTFullLt, 0, -1500, 1500)                                                                         \
  P(NmpImprovingLt, 0, -100, 100)                                                                          \
  P(QuietFutAlphaLt, 0, -200, 200)                                                                         \
  /* gestione del tempo (in millesimi) */                                                                 \
  P(TmFallBase, 1148, 500, 2500)         /* /100: fallingEval = (base + a*(prev-best) + b*(iter-best)) */ \
  P(TmFallPrev, 230, 0, 600)                                                                              \
  P(TmFallIter, 110, 0, 400)                                                                              \
  P(TmInstabBase, 1077, 500, 2000)                                                                        \
  P(TmInstabMul, 2229, 0, 5000)                                                                           \
  P(TmFenPly, 0, 0, 1)                   /* 1: ply di partita = numero di mossa della FEN + mosse giocate */ \
  /* Risposta prevista (nostra, 05/10/2026, idea dell'utente): se l'avversario gioca la risposta della nostra linea  \
     principale, la hash e' gia' calda e la mossa che avevamo previsto di solito regge. Il tempo della mossa si        \
     riduce, ma solo quando la ricerca ha ritrovato la profondita' di prima (meno TmExpectDepth), la mossa migliore  \
     e' quella prevista e la valutazione non e' scesa oltre TmExpectMargin. A 1000 spenta: tempi identici. */        \
  P(TmExpectScale, 1000, 200, 1000)      /* /1000: fattore sul tempo della mossa (1000 = spenta) */          \
  P(TmExpectDepth, 2, 0, 8)              /* da profondita' della mossa prima - questo valore */             \
  P(TmExpectMargin, 70, 0, 400)          /* calo massimo della valutazione, unita' dei margini (70 ~ 20 cp) */ \
  /* Pavimento (05/10/2026): misurato che dopo la risposta prevista la regola della stabilita' (time_red) ferma gia' \
     la ricerca prestissimo, anche SOTTO la profondita' della mossa prima (0,5 s a prof. 16 dopo una ricerca a 19;    \
     da hash vuota 3,2 s a 17 con un'altra mossa). Con TmExpectFloor = k > 0 non ci si ferma prima di prof. di prima \
     - k (salvo il tempo massimo): con la hash calda quelle iterazioni costano poco. 0 = spento. */                   \
  P(TmExpectFloor, 0, 0, 8)                                                                                \
  /* Sorpresa (nostra, 05/10/2026, idea dell'utente; lo spunto e' un'osservazione di Mark Tang su Stoofvlees, che    \
     risponde subito fra due mosse anche a TC lungo): il rovescio della risposta prevista. Se l'avversario NON gioca  \
     la risposta della nostra linea principale, la posizione e' meno chiara di quanto pensavamo: il tempo della      \
     mossa x TmSurpriseScale/1000. Il tempo che la regola della stabilita' risparmia sulle risposte previste va cosi' \
     alle mosse incerte. A 1000 spenta: tempi identici.                                                              \
     ⚠️ DIPENDE MOLTO DAL TC (utente, 05/10/2026): x1,2 neutra a 16+0.16 (+0,7 +- 7,1 su 2.446), positiva a 40+0.4  \
     (+6,23 +- 6,75 su 2.174, dai PGN +0,35-0,4 ply sulle mosse dopo una sorpresa). Se entra in uno SPSA va tarata  \
     AL TC DI DESTINAZIONE (lungo), oppure tenuta fissa: a TC corto lo SPSA la spingerebbe verso 1000. */            \
  P(TmSurpriseScale, 1200, 1000, 2000)   /* BAKE 05/10/2026 (era 1000, vedi in testa): x1,2 dopo una sorpresa */ \
  /* Soglia (05/10/2026, utente): la sorpresa vale solo finche' l'orologio e' ancora sopra questa percentuale di     \
     quello di inizio partita. Con poco tempo non ci si puo' fermare a pensare: si gioca al tempo ottimale. A 16+0.16 \
     senza soglia, x1,2: +0,7 +- 9,0 su 1.480 partite (dai PGN: sorprese ~25% delle mosse fino alla 50, 52% nei      \
     finali, dove l'orologio e' piu' corto). Con soglia 40 e x1,6 (un ply in piu' sulle sorprese) a 20+0.2: -10,1 +- \
     7,5 su 1.998 partite, chiusa: il tempo preso al mediogioco costa piu' di quanto rende. 0 = sempre. */           \
  P(TmSurpriseClock, 0, 0, 100)          /* % dell'orologio di inizio partita sotto cui la sorpresa tace */  \
  /* Ramo con incremento (blitz di CCRL), 05/10/2026: le costanti di clock_plan come parametri per uno SPSA del  \
     tempo contro un motore esterno (in self-play il lato che spende di piu' vince). Default = valori di prima: la \
     divisione IEEE e' arrotondata correttamente (12112 / 1e6 == 0.012112). */                                  \
  P(TmOptBase, 12112, 4000, 30000)       /* /1e6: quota ottimale = base + (ply + 3.23)^pow * c */           \
  P(TmOptPow, 46866, 25000, 70000)       /* /1e5: crescita della quota col ply di partita */                \
  P(TmOptC, 29869, 12000, 50000)         /* /1e7: c = TmOptC + TmOptCLog * log10(secondi sull'orologio) */   \
  P(TmOptCLog, 33554, 0, 90000)          /* /1e8 */                                                         \
  P(TmOptClockCap, 19404, 8000, 32000)   /* /1e5: tetto della quota ottimale sul tempo rimasto */           \
  P(TmMaxCBase, 33744, 20000, 50000)     /* /1e4: tempo massimo = (base + log * log10 s + ply/div) x ottimo */ \
  P(TmMaxCLog, 30608, 0, 60000)          /* /1e4 */                                                         \
  P(TmMaxPlyDiv, 12352, 4000, 30000)     /* /1e3 */                                                         \
  P(TmMaxClock, 8097, 6500, 8800)        /* /1e4: il massimo non supera questa frazione dell'orologio */    \
  /* idea 8: ramo movestogo (il 40/15 di CCRL), finora costanti mai tarate su di noi; default = valori attuali */ \
  P(TmMtgBase, 880, 300, 2000)           /* /1000: quota ottimale = (base + ply*1000/TmMtgPly) / orizzonte */ \
  P(TmMtgPly, 1164, 300, 4000)           /* /10 */                                                        \
  P(TmMtgClock, 880, 300, 1000)          /* /1000: tetto della quota sul tempo rimasto */                 \
  P(TmMtgMaxBase, 1300, 800, 3000)       /* /1000: massimo = (base + TmMtgMaxHorizon*orizzonte) x ottimo */ \
  P(TmMtgMaxHorizon, 110, 0, 400)        /* /1000 */

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
static bool g_ponder_option      = false;    // "Ponder": la GUI pondera -> +25% di tempo ottimale
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
int       g_tt_keep_margin = 3;   // si sovrascrive se la nuova non e' piu' corta di 4 ply (+2 sui nodi PV)
bool      g_tt_move_keep  = true; // senza mossa nuova si conserva quella vecchia
bool      g_tt_move_refresh = false; // copia di TTMoveRefresh per tt.h (vedi store_tt)
int       g_see_pinned    = 0;    // copia di SeePinned per see.cpp (vedi td_see_ge)
bool      g_tt_age_refresh = false;
bool      g_large_pages   = true;
bool      g_see_fix       = true;
bool      g_ep_key_fix    = true;
static const bool g_evasion_gen = true;

// Codice di ogni pezzo nella posizione della rete, per pezzo di Triumviratus (P,N,B,R,Q,K,p,n,b,r,q,k).
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
  // Le opzioni lette fuori da threads.cpp (tt.h, see.cpp) hanno una copia globale: si riallinea a ogni setoption.
  const auto sync = [] { g_tt_move_refresh = TTMoveRefresh != 0; g_see_pinned = SeePinned; };
#define TP_SET(pname, def, lo, hi)                                                                         \
  if (!strcmp(name, #pname)) { pname = value < lo ? lo : (value > hi ? hi : value); sync(); return true; }
  TRIUMV_SEARCH_PARAMS(TP_SET)
#undef TP_SET
#endif
  return false;
}
