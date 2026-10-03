// search/11_qsearch.inc -- Quiescenza.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ============================================================================
// QUIESCENCE SEARCH
// ============================================================================

// QsStalemateCheck support: movegen COMPLETO (catture + quiete), si ferma
// alla prima mossa legale trovata. Costo O(1) nel caso comune (un finale K+P
// ha quasi sempre una mossa di pedone/re); la scansione dell'intera lista
// avviene solo nel vero stallo, che e' esattamente il caso raro che questo
// probe deve scovare. Chiamata SOLO dal trigger gated in td_quiescence.
static bool td_has_any_legal_move(ThreadData &td) {
  moves ml[1];
  td_generate_moves(td, ml, false);
  for (int i = 0; i < ml->count; i++) {
    UndoInfo undo;
    td.ply++;
    td.repetition_table[++td.repetition_index] = td.hash_key;
    bool legal = td_make_move(td, ml->moves[i], undo) != 0;
    if (legal)
      td_unmake_move(td, ml->moves[i], undo);
    td.ply--;
    td.repetition_index--;
    if (legal)
      return true;
  }
  return false;
}

// qs_depth: 0 alla prima ply di qsearch (chiamate dal negamax), decresce nelle
// ricorsioni. Serve solo a P1.3 QSChecks (quiet check tenuti SOLO a qs_depth
// 0).
// NPS 25/09/2026 — risultato GREZZO di un probe TT gia' fatto sulla stessa posizione.
// td_negamax a depth <= 0 interrogava la TT (per le sue regole di cutoff) e poi passava
// alla qsearch, che la interrogava di nuovo sulla stessa chiave senza che nel frattempo
// fosse stato scritto nulla. Ora il primo probe viene passato. Nodi identici: stesso
// risultato, e il refresh d'eta' del primo probe e' gia' avvenuto (il secondo non
// scriveva piu' niente).
struct TtProbeData {
  bool hit;
  int move, score, depth, flag, eval;
  bool pv;
};

static int td_quiescence(ThreadData &td, int alpha, int beta,
                         int qs_depth = 0, const TtProbeData *pre = nullptr) {
  SSTAT(QNODES, 0);
  // Illegal-position / king-capture guard (see td_negamax for the full
  // rationale): never search a position where the side not to move is in check,
  // because making the king capture desyncs the NNUE accumulator and crashes.
  // (Rimozione provata 2026-06-06: node-identica ma NPS ~0 -> tenuta per
  // safety.) NPS 25/09/2026: gate ply==0, stesso ragionamento di td_negamax.
  if (td.ply == 0)
  {
    int opp_king_sq =
        get_ls1b_index((td.side == white) ? td.bitboards[k] : td.bitboards[K]);
    if (td_is_square_attacked(td, opp_king_sq, td.side))
      return mate_value - td.ply;
  }

  // F-015 (RIPRISTINATO A TOGGLE 2026-07-04 sera): draw-detection anche in
  // qsearch (SF is_draw). L'hardening precedente e' stato ANNULLATO — il test
  // nps_ab_test.py a 80 posizioni dava -1.2% nodi (trascurabile), ma il bench a
  // 8 posizioni dava +17.6% (781516 vs 664743): scarto troppo grande per essere
  // solo "posizioni diverse", da capire prima di ri-proporre l'hardening.
  // Default OFF finche' non e' chiarito il meccanismo.

  if ((td.nodes & 4095) == 0) {
    if (stop_threads.load(std::memory_order_relaxed))
      return 0;
    // A5: `td_root_move_ready()` = pavimento anti-forfeit, vedi la definizione.
    // Short-circuit: non viene mai valutata finche' il tempo non e' scaduto.
    if (timeset && get_time_ms() > stoptime && td_root_move_ready()) {
      stop_threads.store(true, std::memory_order_relaxed);
      return 0;
    }
  }

  td.nodes++;
  if (g_node_limit_thread && td.nodes >= g_node_limit_thread) {
    stop_threads.store(true, std::memory_order_relaxed);
    return 0;
  }
  if (td.ply > td.seldepth)
    td.seldepth = td.ply; // seldepth (info UCI)

  if (td.ply >= max_ply)
    return td_evaluate(td);

  bool pv_node = (beta - alpha > 1);

  // TT probe (qsearch entries stored at depth 0)
  int tt_move = 0, tt_score = 0, tt_depth = 0, tt_flag = hash_flag_alpha;
  int tt_eval = tt_eval_none; // P1.1
  bool tt_pv_q = false;
  bool tt_hit;
  if (pre) {
    tt_hit = pre->hit;
    tt_move = pre->move;
    tt_score = pre->score;
    tt_depth = pre->depth;
    tt_flag = pre->flag;
    tt_eval = pre->eval;
    tt_pv_q = pre->pv;
  } else
    tt_hit = probe_tt(td.hash_key, tt_move, tt_score, tt_depth, tt_flag,
                      tt_eval, tt_pv_q);
  if (tt_hit && !pv_node) {
    if (tt_score < -mate_score)
      tt_score += td.ply;
    if (tt_score > mate_score)
      tt_score -= td.ply;
    if (g_tt_decisive_clamp)
      tt_score = tt_clamp_decisive(tt_score, td.fifty); // Q-32
    if (tt_flag == hash_flag_exact)
      return tt_score;
    if (tt_flag == hash_flag_alpha && tt_score <= alpha)
      return tt_score;
    if (tt_flag == hash_flag_beta && tt_score >= beta)
      return tt_score;
  }

  // Sotto scacco: niente stand-pat, si cercano TUTTE le evasioni.
  // NPS 25/09/2026: se il padre ha appena calcolato gives_check per questa mossa, lo
  // scacco al nodo e' gia' noto (vedi chk_hint in threads.h). Stesso valore, un test
  // di attacco in meno per nodo.
  const int chk_hint = td.chk_hint[td.ply];
  bool in_check;
  if (chk_hint >= 0)
    in_check = chk_hint != 0;
  else {
    int king_sq =
        get_ls1b_index((td.side == white) ? td.bitboards[K] : td.bitboards[k]);
    in_check = td_is_square_attacked(td, king_sq, td.side ^ 1);
  }

  int best_score;
  int q_raw_eval =
      tt_eval_none; // P1.1: eval pura da salvare nello store qsearch
  if (in_check) {
    best_score = -infinity;
  } else {
    // P1.1 TTStaticEval: come nel negamax. flag_none = entry eval-only
    // (EvalTTWrite): UNADJUSTED -> nn_finalize(fifty corrente), esatto su ogni
    // trasposizione.
    q_raw_eval =
        (g_tt_static_eval && tt_eval != tt_eval_none)
            ? (tt_flag == hash_flag_none
                   ? nn_finalize(tt_eval, td.fifty,
                                (count_bits(td.occupancies[both]) - 1) / 4)
                                         : tt_eval_redamp(tt_eval, td.fifty))
            : td_evaluate(td);
    int stand_pat = q_raw_eval;
    // FIX P0.4 (2026-06-09): la correction history ora corregge ANCHE lo
    // stand-pat di quiescence (dove avviene la maggioranza delle eval);
    // prima si fermava ai nodi interni. Gated QsearchCorr per l'ablazione.
    if (g_qsearch_corr && g_corr_hist)
      stand_pat += td_corr_value(td, td_corr_index(td));
    // P1.1: tt_score con bound coerente = stima di qualita'-search migliore
    // dello stand-pat statico (stesso adjustment del negamax).
    if (g_tt_eval_improve && tt_hit && tt_score > -mate_score &&
        tt_score < mate_score &&
        (tt_flag == hash_flag_exact ||
         (tt_flag == hash_flag_beta && tt_score > stand_pat) ||
         (tt_flag == hash_flag_alpha && tt_score < stand_pat)))
      stand_pat = tt_score;
    best_score = stand_pat;
    // F-018.7 FailHighSmooth: midpoint verso beta sul fail-high statico
    // (Obsidian/Berserk).
    if (stand_pat >= beta)
      return (g_fh_smooth && stand_pat < mate_score && stand_pat > -mate_score)
                 ? td_fail_firm(stand_pat, beta, g_fh_t_qs_standpat)
                 : stand_pat;

    // F-002: era `const int DELTA = 1000` (unita'-eval, mai ricalibrato
    // post-EvalScale). Q-20d (Ethereal): delta-pruning col BEST-CASE reale —
    // vittima piu' grossa sul board + potenziale promozione (pedone in 7a
    // relativa). Se nemmeno il colpo migliore possibile + cuscino arriva ad
    // alpha, il nodo e' morto.
    if (stand_pat + g_qs_delta < alpha)
      return stand_pat;

    if (stand_pat > alpha)
      alpha = stand_pat;
  }

  moves move_list[1];
  // Se non siamo sotto scacco (!in_check), passa true per generare SOLO le
  // catture. P1.3 QSChecks (default OFF): alla prima ply di qsearch genera
  // TUTTO, poi il loop tiene solo catture + quiet che danno SCACCO DIRETTO.
  const bool qs_checks_here = false && qs_depth == 0 && !in_check;
  // QSTTQuiets (Stormphrax search.cpp:1557): genera anche le quiete quando la TT
  // dice che la best e' quieta E il bound e' affidabile. `hash_flag_alpha` e' il
  // nostro upper bound (fail-low), quindi escluderlo = il loro
  // `flag != UpperBound`. Nessun cap di qs_depth: la condizione TT e' gia' molto
  // piu' stretta di "prima ply". OFF -> byte-identico.
  const bool qs_tt_quiets = 0 && !pv_node && !in_check && tt_hit &&
                            tt_move && tt_flag != hash_flag_alpha &&
                            !get_move_capture(tt_move) &&
                            !get_move_promoted(tt_move);
  td_generate_moves(td, move_list,
                    !in_check && !qs_checks_here && !qs_tt_quiets,
                    /*quiets_only=*/false, /*known_in_check=*/-1,
                    /*promo_quiet=*/(g_promo_qs >= 1 && g_promo_qs <= 3) ||
                                     g_promo_qs >= 5,
                    /*promo_queen_only=*/g_promo_qs >= 2,
                    /*promo_knight=*/g_promo_qs == 7);

  // Calcola punteggi senza ordinare (selezione pick-next).
  int move_scores[256];
  td_score_all_moves(td, move_list, tt_move, move_scores);

  int best_move = 0;
  int legal_moves = 0;
  // Modo 5: le promozioni non consumano uno slot di QSMoveCap. Senza questo,
  // scoring 750.000 + cap 3 = tre promozioni e zero catture.
  int qs_cap_count = 0;
  int qcaptures =
      0; // catture ESAMINATE a questo nodo (per BadNoisy move-count pruning)
  int q_searched_caps[8]; // Q-17 QSCaptHist: catture cercate legalmente (cap 8,
                          // come Caissa)
  int nq_caps = 0;
  // QSCapRecap: casa d'arrivo dell'ultima mossa avversaria, cercata fra le mosse
  // RIMASTE. -2 = non ancora calcolata, -1 = nessuna ricattura residua (si esce
  // col break di sempre). Calcolata UNA volta sola e SOLO a cap gia' pieno:
  // senza, il loop scandirebbe tutta la lista col pick-next (selection sort) per
  // non cercare niente = O(n^2) su ogni nodo a cap pieno.
  int qcr_sq = -2;

  for (int count = 0; count < move_list->count; count++) {

    // F-018.14 QSMoveCap (0=off): le mosse sono ordinate best-first -> oltre le
    // prime N (non in scacco) il resto e' quasi sempre inutile (Obsidian: 3).
    // Il contatore resta quello di PromoQS (qs_cap_count nel modo >= 5, dove le
    // promozioni non consumano slot): QSCapRecap non lo tocca, filtra soltanto
    // COSA passa dopo che il cap e' stato raggiunto.
    const bool qs_over_cap =
        g_qs_move_cap && !in_check &&
        (g_promo_qs >= 5 ? qs_cap_count : legal_moves) >= g_qs_move_cap;
    if (qs_over_cap) {
      break;
      if (qcr_sq == -2) {
        int qcr_prev = td.move_stack[td.ply];
        qcr_sq = -1;
        if (qcr_prev) {
          int ps = get_move_target(qcr_prev);
          for (int i = count; i < move_list->count; i++)
            if (get_move_target(move_list->moves[i]) == ps) {
              qcr_sq = ps;
              break;
            }
        }
      }
      if (qcr_sq < 0)
        break;
    }

    // --- INIZIO PICK-NEXT: Cerca la mossa migliore tra quelle rimaste ---
    int best_idx = count;
    {
      int bsc = move_scores[count]; // NPS 25/09/2026: senza salti (stessa scelta)
      for (int i = count + 1; i < move_list->count; i++) {
        const int v = move_scores[i];
        const bool b = v > bsc;
        bsc = b ? v : bsc;
        best_idx = b ? i : best_idx;
      }
    }

    // Scambiamo mossa e score per portarli in cima
    if (best_idx != count) {
      int tmp_move = move_list->moves[count];
      move_list->moves[count] = move_list->moves[best_idx];
      move_list->moves[best_idx] = tmp_move;

      int tmp_score = move_scores[count];
      move_scores[count] = move_scores[best_idx];
      move_scores[best_idx] = tmp_score;
    }

    // Ora move   garantita essere la migliore disponibile
    int move = move_list->moves[count];
    // --- FINE PICK-NEXT ---

    // QSCapRecap: a cap esaurito sopravvive SOLO la ricattura sulla casa appena
    // mossa dall'avversario (qcr_sq, dallo stesso td.move_stack[td.ply] che
    // usano BadNoisy e QFutility). Non serve testare get_move_capture: quella
    // casa ospita il pezzo avversario appena arrivato. Il filtro sta DOPO il
    // pick-next, cosi' fra piu' ricatture si sceglie la migliore per score.
    if (qs_over_cap && get_move_target(move) != qcr_sq)
      continue;

    if (!in_check) {
      // PromoQS: la promozione quieta ha il flag capture a 0, quindi senza questa
      // esenzione il filtro qui sotto la scarterebbe subito dopo averla generata.
      // 🔴 Modo 6: e SOLO se non regala la Donna. Promuovere su una casa dove il
      // pezzo viene ripreso apre un sottoalbero intero (Donna nuova = tutte le sue
      // catture e tutte le riprese) per una mossa che perde materiale. E' il
      // filtro che spiega perche' il modo 5 costa +18,9% di albero.
      if (g_promo_qs >= 6 && get_move_promoted(move) &&
          !get_move_capture(move) && !td_see_at_least(td, move, 0))
        continue;
      if (!get_move_capture(move) &&
          !(g_promo_qs && get_move_promoted(move))) {
        // QSTTQuiets: quando la condizione TT vale, le quiete passano SENZA il
        // filtro "deve dare scacco" — e' la forma di Stormphrax, dove il
        // generatore produce tutte le mosse e a limitare sono l'ordinamento
        // best-first, il SEE e il cap di move-count (da noi QSMoveCap=3).
        // Il filtro vero e' gia' nella condizione TT, che e' rarissima.
        // modo 1 = tutte le quiete ammesse (Stormphrax). modo 2 = ammesse solo
        // quelle che danno scacco, cioe' il filtro qui sotto resta attivo ma il
        // gate d'ingresso e' la condizione TT invece di "prima ply".
        if (0 != 1) {
          // P1.3 QSChecks: tieni il quiet SOLO se da' scacco diretto e non
          // e' un check-blunder (SEE >= -75, stesso filtro del CheckOrdering).
          if (!qs_checks_here && !qs_tt_quiets)
            continue;
          if (td.check_key != td.hash_key)
            td_compute_checks(td);
          if (!get_bit(td.check_sq[get_move_piece(move) % 6],
                       get_move_target(move)))
            continue;
          if (!td_see_at_least(td, move, -75))
            continue;
        }
      } else {
        // BadNoisy (SF qsearch move-count pruning, gated OFF): le catture sono
        // ordinate best-first -> dopo le prime N le rimanenti sono quasi-sempre
        // perdenti. Niente skip su ricattura (to != prevSq) ne' promo/ep.
        if (g_bad_noisy && !get_move_promoted(move) &&
            !get_move_enpassant(move)) {
          int prev_bn = td.move_stack[td.ply];
          int prev_bn_sq = prev_bn ? get_move_target(prev_bn) : -1;
          if (get_move_target(move) != prev_bn_sq &&
              qcaptures >= g_bad_noisy_count)
            continue;
        }
        qcaptures++;
        // (4) QFutility: salta la cattura se best_score + valore(vittima) +
        // margine
        //     non arriva ad alpha (no ep/promo). OFF = nessun effetto.
        //     FIX SCALA (2026-06-24): best_score/alpha sono eval-NET (~3x la
        //     scala-100; pedone marginale misurato ~190-500cp via `eval`),
        //     mentre see_piece_values e' scala-100 -> il termine vittima va
        //     scalato, altrimenti la qsearch sovra-pota (era -126 Elo). La
        //     sovrastima (fattore ~3 > del capfut 2.35) e' voluta: pota MENO le
        //     catture grosse = errore nella direzione SICURA (l'eval-net satura).
        //     QFutMargin tara il residuo.
        //     FIX DRIFT (2026-07-24): il fattore era hardcoded "* 3" (tarato a
        //     EvalScale=56, mai ri-seguito) -> ora g_qfut_vic_scale *
        //     nn_get_eval_scale() / 10000, come il gemello capfut: byte-identico
        //     oggi (500*60/10000=3.0) ma auto-ricalibrante ai bake della rete.
        if (!get_move_promoted(move) && !td_see_at_least(td, move, 0))
          continue;
      }
    }

    // QsStalemateCheck: la cattura toglie l'ultima Torre/Donna dalla
    // scacchiera (materiale pre-mossa mascherato sulla casa target)? Calcolato
    // PRIMA di muovere (serve il pezzo-vittima). Vedi definizione toggle sopra
    // e td_has_any_legal_move.
    bool cap_last_major = false;
    if (g_qs_stalemate_check && get_move_capture(move)) {
      int vic = td_captured_piece(td, get_move_target(move));
      if (vic == R || vic == Q || vic == r || vic == q) {
        U64 remaining_majors = (td.bitboards[R] | td.bitboards[Q] |
                                 td.bitboards[r] | td.bitboards[q]) &
                                ~(1ULL << get_move_target(move));
        cap_last_major = (remaining_majors == 0);
      }
    }

    UndoInfo undo;
    td.ply++;
    td.repetition_table[++td.repetition_index] = td.hash_key;

    if (!td_make_move(td, move, undo)) {
      td.ply--;
      td.repetition_index--;
      continue;
    }

    legal_moves++;
    if (!get_move_promoted(move))
      qs_cap_count++;  // modo 5: la promozione non consuma uno slot del cap
    td.move_stack[td.ply] = move; // mantieni il move-stack in qsearch (serve
                                  // alla recapture-exemption della QFutility)
    if (nq_caps < 8 && get_move_capture(move))
      q_searched_caps[nq_caps++] = move; // Q-17

    int score;
    if (cap_last_major) {
      int child_king_sq =
          get_ls1b_index((td.side == white) ? td.bitboards[K] : td.bitboards[k]);
      bool child_in_check = td_is_square_attacked(td, child_king_sq, td.side ^ 1);
      score = (!child_in_check && !td_has_any_legal_move(td))
                  ? 0 // stallo: pareggio morto, ne' catture ne' scacchi ne'
                      // mosse tranquille — qsearch normale non lo avrebbe MAI
                      // visto (genera solo catture)
                  : -td_quiescence(td, -beta, -alpha, qs_depth - 1);
    } else {
      score = -td_quiescence(td, -beta, -alpha, qs_depth - 1);
    }

    td_unmake_move(td, move, undo);
    td.ply--;
    td.repetition_index--;

    if (stop_threads.load(std::memory_order_relaxed))
      return 0;

    if (score > best_score) {
      best_score = score;
      best_move = move;
      if (score > alpha) {
        alpha = score;
        if (score >= beta)
          break;
      }
    }
  }

  if (in_check && legal_moves == 0)
    return -mate_value + td.ply;

  // Q-17 QSCaptHist (Caissa Search.cpp:1169-1245, default OFF): la maggioranza
  // delle catture passa in qsearch ma la capture-history imparava solo dal main
  // search. Al beta-cutoff: bonus alla cattura vincente, malus alle provate
  // prima (board gia' ripristinata dal loop -> td_captured_piece legge i pezzi
  // giusti).
  if (g_qs_capthist && best_score >= beta && best_move &&
      get_move_capture(best_move)) {
    int qb = td_stat_bonus(1) * g_qs_capthist_scale / 100;
    int bvic = td_captured_piece(td, get_move_target(best_move));
    if (bvic >= 0 && bvic < 12)
      td_update_history(
          td.capture_history[get_move_piece(best_move)]
                            [get_move_target(best_move)][bvic]
                            [td_cbucket(td, get_move_target(best_move))],
          qb);
    int qmal = qb * g_malus_pct / 100;
    for (int c = 0; c < nq_caps; c++) {
      int cm = q_searched_caps[c];
      if (cm == best_move)
        continue;
      int cvic = td_captured_piece(td, get_move_target(cm));
      if (cvic >= 0 && cvic < 12)
        td_update_history(
            td.capture_history[get_move_piece(cm)][get_move_target(cm)][cvic]
                              [td_cbucket(td, get_move_target(cm))],
            -qmal);
    }
  }

  // F-018.7 FailHighSmooth: midpoint verso beta anche sul best finale (come
  // Obsidian, che lo salva cosi' pure in TT: bound meno gonfiato = meno
  // re-search dei padri).
  if (g_fh_smooth && best_score >= beta && best_score < mate_score &&
      best_score > -mate_score)
    best_score = td_fail_firm(best_score, beta, g_fh_t_qs_final);

  int store_flag = (best_score >= beta) ? hash_flag_beta : hash_flag_alpha;
  store_tt(td.hash_key, best_move, best_score, 0, store_flag, td.ply, false,
           tt_store_eval(q_raw_eval, tt_eval, tt_flag, td.fifty));

  return best_score;
}
