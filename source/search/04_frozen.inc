// search/04_frozen.inc -- Parametri congelati (TRIUMV_FROZEN): tabella di controllo e #define.
// Parte di threads.cpp: NON e' un'unita' di compilazione a se'. Va inclusa solo da threads.cpp, nell'ordine
// fissato li' (divisione del 27/09/2026, codice generato identico: vedi l'intestazione di threads.cpp).
// ===== PARAMETRI CONGELATI (TRIUMV_FROZEN) ==============================
// I 303 interruttori di tuning qui sotto (302 dall'analisi del 09/09, piu' g_corr_major) sono globali che, passata la fase di
// setoption, nessuno riscrive piu'. L'elenco NON e' scritto a mano: viene da
// un'analisi dei siti di assegnazione in TUTTE le unita' di compilazione, e
// tiene solo cio' che e' scritto esclusivamente da set_search_param o da un
// setter set_*, tutti raggiungibili solo da setoption, cioe' prima di `go`.
// L'unico escluso e' g_searchmoves_count, che uci_mt.cpp riempie a OGNI `go`:
// e' stato mutabile travestito da parametro, e la prima versione di questa
// analisi se l'era mangiato perche' guardava solo dentro threads.cpp.
//
// Perche' congelarli. Lasciarli variabili costringe il compilatore a ricaricarli
// e a ramificarci sopra a ogni mossa segnata: dentro td_score_move ce ne sono 26.
// Misurato sul prototipo delle sole 26: -147 istruzioni/nodo, -1,6% di cicli,
// firma bench invariata a 242956. E' la differenza strutturale con Stockfish,
// che di interruttori a runtime non ne ha: 780 salti per nodo contro i suoi 458.
//
// 🔴 Il pericolo. setoption resta attivo anche nelle build di spedizione, quindi
// qualcuno puo' cambiare un'opzione congelata e vedere il motore ignorarla in
// silenzio. Per questo la tabella qui sotto tiene l'indirizzo di ogni variabile
// VERA - e' compilata PRIMA delle #define, dove `&g_x` e' ancora la variabile -
// e triumv_frozen_check() la confronta a ogni ricerca, dicendolo sul canale che
// la GUI legge. Un congelamento silenzioso sarebbe un bug irrintracciabile.
//
// Rigenerare dopo un bake: i valori vanno letti dalle DICHIARAZIONI vive, non
// dai commenti che le accompagnano. I commenti hanno gia' mentito in passato.
// AUDIT D (T7, 27/09/2026): UCI_ShowWDL, SyzygyProbeDepth e SyzygyProbeLimit tolti dal congelamento. Sono
// opzioni standard che la release pubblicizza e che GUI e tester impostano davvero (la build le ignorava,
// avvisando solo con un info string). Stanno fuori dal percorso caldo: nessun costo misurabile.
#ifdef TRIUMV_FROZEN
struct TriumvFrozenRef { const char* name; const void* p; int isBool; long long val; };
static const TriumvFrozenRef g_frozen_refs[] = {
    {"g_aggr_lmr", (const void*)&g_aggr_lmr, 1, 0},
    {"g_aggr_lmr_clamp", (const void*)&g_aggr_lmr_clamp, 0, 4},
    {"g_aggr_lmr_div", (const void*)&g_aggr_lmr_div, 0, 903},
    {"g_alpha_depth_dec", (const void*)&g_alpha_depth_dec, 1, 1},
    {"g_alpha_depth_dec_amt", (const void*)&g_alpha_depth_dec_amt, 0, 1},
    {"g_bad_noisy", (const void*)&g_bad_noisy, 1, 1},
    {"g_bad_noisy_count", (const void*)&g_bad_noisy_count, 0, 7},
    {"g_badcap_skip_after", (const void*)&g_badcap_skip_after, 0, 1},
    {"g_brilliant_sac", (const void*)&g_brilliant_sac, 1, 0},
    {"g_brilliant_sac_lmr", (const void*)&g_brilliant_sac_lmr, 1, 0},
    {"g_brilliant_sac_lmr_amt", (const void*)&g_brilliant_sac_lmr_amt, 0, 2},
    {"g_brilliant_sac_margin", (const void*)&g_brilliant_sac_margin, 0, 5000},
    {"g_cap_futility", (const void*)&g_cap_futility, 1, 1},
    {"g_capfut_base", (const void*)&g_capfut_base, 0, 159},
    {"g_capfut_chist", (const void*)&g_capfut_chist, 0, 362},
    {"g_capfut_depth", (const void*)&g_capfut_depth, 0, 8},
    {"g_capfut_mult", (const void*)&g_capfut_mult, 0, 231},
    {"g_capfut_vic_scale", (const void*)&g_capfut_vic_scale, 0, 570},
    {"g_caphist_div", (const void*)&g_caphist_div, 0, 21},
    {"g_caphist_threat", (const void*)&g_caphist_threat, 1, 0},
    {"g_capture_hist", (const void*)&g_capture_hist, 1, 1},
    {"g_captured_mailbox", (const void*)&g_captured_mailbox, 0, 1},
    {"g_check_bonus", (const void*)&g_check_bonus, 0, 13357},
    {"g_check_ext_depth", (const void*)&g_check_ext_depth, 0, 0},
    {"g_check_ordering", (const void*)&g_check_ordering, 1, 1},
    {"g_cmhc_ply1", (const void*)&g_cmhc_ply1, 0, 0},
    {"g_cmhc_scale", (const void*)&g_cmhc_scale, 0, 6},
    {"g_cont_hist_prune", (const void*)&g_cont_hist_prune, 1, 0},
    {"g_conthist36", (const void*)&g_conthist36, 1, 1},
    {"g_conthist36_weight", (const void*)&g_conthist36_weight, 0, 37},
    {"g_conthist_lmr", (const void*)&g_conthist_lmr, 1, 1},
    {"g_conthist_multi", (const void*)&g_conthist_multi, 1, 0},
    {"g_corr_major", (const void*)&g_corr_major, 1, 1},
    {"g_conthist_prune_depth", (const void*)&g_conthist_prune_depth, 0, 2},
    {"g_conthist_red_div", (const void*)&g_conthist_red_div, 0, 3684},
    {"g_conthist_weight", (const void*)&g_conthist_weight, 0, 144},
    {"g_corr_asym", (const void*)&g_corr_asym, 0, 137},
    {"g_corr_cap", (const void*)&g_corr_cap, 0, 48},
    {"g_corr_cont", (const void*)&g_corr_cont, 1, 1},
    {"g_corr_cont_weight", (const void*)&g_corr_cont_weight, 0, 77},
    {"g_corr_faillow_all", (const void*)&g_corr_faillow_all, 0, 0},
    {"g_corr_hist", (const void*)&g_corr_hist, 1, 1},
    {"g_corr_lr_div", (const void*)&g_corr_lr_div, 0, 303},
    {"g_corr_material", (const void*)&g_corr_material, 1, 0},
    {"g_corr_material_weight", (const void*)&g_corr_material_weight, 0, 67},
    {"g_corr_trans_weight", (const void*)&g_corr_trans_weight, 0, 0},
    {"g_corr_multi", (const void*)&g_corr_multi, 1, 1},
    {"g_corr_nonpawn", (const void*)&g_corr_nonpawn, 1, 0},
    {"g_corr_np_weight", (const void*)&g_corr_np_weight, 0, 100},
    {"g_corr_tb_guard", (const void*)&g_corr_tb_guard, 0, 1},
    {"g_corr_uncert", (const void*)&g_corr_uncert, 1, 1},
    {"g_corrval_ext", (const void*)&g_corrval_ext, 1, 1},
    {"g_corrval_fut", (const void*)&g_corrval_fut, 0, 38},
    {"g_corrval_lmr", (const void*)&g_corrval_lmr, 0, 83},
    {"g_corrval_margin", (const void*)&g_corrval_margin, 1, 1},
    {"g_corrval_rfp", (const void*)&g_corrval_rfp, 0, 41},
    {"g_corrval_see", (const void*)&g_corrval_see, 0, 27},
    {"g_countermove", (const void*)&g_countermove, 1, 1},
    {"g_cu_cap", (const void*)&g_cu_cap, 0, 70},
    {"g_cu_fut", (const void*)&g_cu_fut, 0, 71},
    {"g_cu_rfp", (const void*)&g_cu_rfp, 0, 60},
    {"g_cutnode_lmr", (const void*)&g_cutnode_lmr, 1, 1},
    {"g_cutnode_lmr_extra", (const void*)&g_cutnode_lmr_extra, 0, 1},
    {"g_cutnode_prop", (const void*)&g_cutnode_prop, 0, 0},
    {"g_cutoff_stats", (const void*)&g_cutoff_stats, 1, 0},
    {"g_cutoffcnt_penalty", (const void*)&g_cutoffcnt_penalty, 0, 2},
    {"g_diverse_smp", (const void*)&g_diverse_smp, 1, 1},
    {"g_do_deeper", (const void*)&g_do_deeper, 1, 0},
    {"g_dodeeper_base", (const void*)&g_dodeeper_base, 0, 43},
    {"g_doshallower_margin", (const void*)&g_doshallower_margin, 0, 7},
    {"g_draw_dither", (const void*)&g_draw_dither, 1, 1},
    {"g_easycap_gate", (const void*)&g_easycap_gate, 1, 0},
    {"g_ep_key_fix", (const void*)&g_ep_key_fix, 1, 1},
    {"g_eval_cache", (const void*)&g_eval_cache, 1, 1},
    {"g_eval_off", (const void*)&g_eval_off, 1, 0},
    {"g_eval_tt_write", (const void*)&g_eval_tt_write, 1, 0},
    {"g_evalcache_opt_split", (const void*)&g_evalcache_opt_split, 1, 0},
    {"g_evalcache_undamp", (const void*)&g_evalcache_undamp, 1, 1},
    {"g_evasion_gen", (const void*)&g_evasion_gen, 1, 1},
    {"g_fast_rep_scan", (const void*)&g_fast_rep_scan, 1, 1},
    {"g_fh_smooth", (const void*)&g_fh_smooth, 1, 1},
    {"g_fh_t_qs_final", (const void*)&g_fh_t_qs_final, 0, 512},
    {"g_fh_t_qs_standpat", (const void*)&g_fh_t_qs_standpat, 0, 512},
    {"g_fh_t_rfp", (const void*)&g_fh_t_rfp, 0, 512},
    {"g_fhboost_margin", (const void*)&g_fhboost_margin, 0, 2},
    {"g_follow_pv", (const void*)&g_follow_pv, 1, 0},
    {"g_fut_base", (const void*)&g_fut_base, 0, 161},
    {"g_fut_depth", (const void*)&g_fut_depth, 0, 7},
    {"g_fut_improving", (const void*)&g_fut_improving, 0, 117},
    {"g_fut_mult", (const void*)&g_fut_mult, 0, 120},
    {"g_fut_spare_quiet", (const void*)&g_fut_spare_quiet, 1, 0},
    {"g_goodcap_hist_div", (const void*)&g_goodcap_hist_div, 0, 32},
    {"g_goodcap_ttquiet", (const void*)&g_goodcap_ttquiet, 1, 0},
    {"g_hindsight_ext", (const void*)&g_hindsight_ext, 1, 1},
    {"g_hindsight_margin", (const void*)&g_hindsight_margin, 0, 3},
    {"g_hindsight_red", (const void*)&g_hindsight_red, 1, 0},
    {"g_hindsight_red_margin", (const void*)&g_hindsight_red_margin, 0, 2},
    {"g_hindsight_red_thresh", (const void*)&g_hindsight_red_thresh, 0, 113},
    {"g_hist_bonus_max", (const void*)&g_hist_bonus_max, 0, 2785},
    {"g_hist_bonus_mult", (const void*)&g_hist_bonus_mult, 0, 471},
    {"g_hist_bonus_sf", (const void*)&g_hist_bonus_sf, 1, 1},
    {"g_hist_bonus_sub", (const void*)&g_hist_bonus_sub, 0, 255},
    {"g_hist_red_div", (const void*)&g_hist_red_div, 0, 3190},
    {"g_hist_triv_guard", (const void*)&g_hist_triv_guard, 1, 0},
    {"g_histprune_margin", (const void*)&g_histprune_margin, 0, 2097},
    {"g_iid", (const void*)&g_iid, 1, 0},
    {"g_iid_depth", (const void*)&g_iid_depth, 0, 4},
    {"g_iid_reduction", (const void*)&g_iid_reduction, 0, 2},
    {"g_iir_amount", (const void*)&g_iir_amount, 0, 1},
    {"g_iir_min_depth", (const void*)&g_iir_min_depth, 0, 4},
    {"g_iir_no_allnode", (const void*)&g_iir_no_allnode, 0, 0},
    {"g_iir_not_in_check", (const void*)&g_iir_not_in_check, 1, 0},
    {"g_improving", (const void*)&g_improving, 1, 1},
    {"g_improving_fix", (const void*)&g_improving_fix, 1, 1},
    {"g_killer_lmr_fix", (const void*)&g_killer_lmr_fix, 1, 1},
    {"g_killer_reset", (const void*)&g_killer_reset, 1, 1},
    {"g_lazy_eval", (const void*)&g_lazy_eval, 1, 1},
    {"g_lmp_base", (const void*)&g_lmp_base, 0, 16},
    {"g_lmp_check_guard", (const void*)&g_lmp_check_guard, 1, 0},
    {"g_lmp_improving", (const void*)&g_lmp_improving, 1, 1},
    {"g_lmp_quad", (const void*)&g_lmp_quad, 0, 141},
    {"g_lmp_scale", (const void*)&g_lmp_scale, 0, 35},
    {"g_lmr_alpha_gap", (const void*)&g_lmr_alpha_gap, 0, 0},
    {"g_lmr_alpha_hi", (const void*)&g_lmr_alpha_hi, 0, 96},
    {"g_lmr_alpha_lo", (const void*)&g_lmr_alpha_lo, 0, 64},
    {"g_lmr_cap_scale", (const void*)&g_lmr_cap_scale, 0, 52},
    {"g_lmr_captures", (const void*)&g_lmr_captures, 1, 1},
    {"g_lmr_ch_div", (const void*)&g_lmr_ch_div, 0, 10942},
    {"g_lmr_check_exempt", (const void*)&g_lmr_check_exempt, 0, 0},
    {"g_lmr_decisive_beta", (const void*)&g_lmr_decisive_beta, 1, 1},
    {"g_lmr_enrich", (const void*)&g_lmr_enrich, 1, 1},
    {"g_lmr_enrich_amount", (const void*)&g_lmr_enrich_amount, 0, 2},
    {"g_lmr_eval_margin", (const void*)&g_lmr_eval_margin, 0, 33},
    {"g_lmr_expect", (const void*)&g_lmr_expect, 0, 0},
    {"g_lmr_fine", (const void*)&g_lmr_fine, 1, 1},
    {"g_lmr_min_depth", (const void*)&g_lmr_min_depth, 0, 3},
    {"g_lmr_min_moves", (const void*)&g_lmr_min_moves, 0, 1},
    {"g_lmr_negative", (const void*)&g_lmr_negative, 0, 0},
    {"g_lmr_ss_div", (const void*)&g_lmr_ss_div, 0, 5430},
    {"g_lmr_ss_offset", (const void*)&g_lmr_ss_offset, 0, -2953},
    {"g_lmr_ttdepth", (const void*)&g_lmr_ttdepth, 0, 1},
    {"g_lmrdepth_histdiv", (const void*)&g_lmrdepth_histdiv, 0, 4509},
    {"g_lmrdepth_prune", (const void*)&g_lmrdepth_prune, 0, 1},
    {"g_lmrf_all", (const void*)&g_lmrf_all, 0, 652},
    {"g_lmrf_cont4", (const void*)&g_lmrf_cont4, 0, 0},
    {"g_lmrf_corr", (const void*)&g_lmrf_corr, 0, 730},
    {"g_lmrf_cut", (const void*)&g_lmrf_cut, 0, 3284},
    {"g_lmrf_cut_nott", (const void*)&g_lmrf_cut_nott, 0, 2142},
    {"g_lmrf_cutoff", (const void*)&g_lmrf_cutoff, 0, 1199},
    {"g_lmrf_evalcut", (const void*)&g_lmrf_evalcut, 0, 1025},
    {"g_lmrf_improv", (const void*)&g_lmrf_improv, 0, 450},
    {"g_lmrf_killer", (const void*)&g_lmrf_killer, 0, 801},
    {"g_lmrf_ply", (const void*)&g_lmrf_ply, 0, 485},
    {"g_lmrf_pv", (const void*)&g_lmrf_pv, 0, 356},
    {"g_lmrf_ss", (const void*)&g_lmrf_ss, 0, 616},
    {"g_lmrf_ttcap", (const void*)&g_lmrf_ttcap, 0, 189},
    {"g_lmrf_ttpv", (const void*)&g_lmrf_ttpv, 0, 640},
    {"g_lowply", (const void*)&g_lowply, 1, 1},
    {"g_lowply_weight", (const void*)&g_lowply_weight, 0, 179},
    {"g_mainhist_weight", (const void*)&g_mainhist_weight, 0, 93},
    {"g_malus_pct", (const void*)&g_malus_pct, 0, 58},
    {"g_malus_quad", (const void*)&g_malus_quad, 1, 0},
    {"g_malus_quad_coef", (const void*)&g_malus_quad_coef, 0, 45},
    {"g_malus_scale_coef", (const void*)&g_malus_scale_coef, 0, 59},
    {"g_malus_scaled", (const void*)&g_malus_scaled, 1, 1},
    {"g_mate_dist", (const void*)&g_mate_dist, 1, 1},
    {"g_move_picker", (const void*)&g_move_picker, 1, 1},
    {"g_multicut", (const void*)&g_multicut, 1, 1},
    {"g_multicut_corr", (const void*)&g_multicut_corr, 1, 0},
    {"g_multicut_corr_scale", (const void*)&g_multicut_corr_scale, 0, 125},
    {"g_multicut_ttmalus", (const void*)&g_multicut_ttmalus, 1, 0},
    {"g_multicut_ttmalus_scale", (const void*)&g_multicut_ttmalus_scale, 0, 100},
    {"g_nc_bonus", (const void*)&g_nc_bonus, 0, 2080},
    {"g_nc_min_sum", (const void*)&g_nc_min_sum, 0, 1066},
    {"g_negext_alpha", (const void*)&g_negext_alpha, 0, 2},
    {"g_negext_cut", (const void*)&g_negext_cut, 0, 3},
    {"g_negext_order", (const void*)&g_negext_order, 0, 0},
    {"g_negext_tt", (const void*)&g_negext_tt, 0, 2},
    {"g_nmp_base", (const void*)&g_nmp_base, 0, 5},
    {"g_nmp_cutnode_only", (const void*)&g_nmp_cutnode_only, 0, 0},
    {"g_nmp_div", (const void*)&g_nmp_div, 0, 3},
    {"g_nmp_eval_div", (const void*)&g_nmp_eval_div, 0, 256},
    {"g_nmp_eval_scale", (const void*)&g_nmp_eval_scale, 1, 0},
    {"g_nmp_sm_bias", (const void*)&g_nmp_sm_bias, 0, 414},
    {"g_nmp_sm_mult", (const void*)&g_nmp_sm_mult, 0, 23},
    {"g_nmp_static_margin", (const void*)&g_nmp_static_margin, 1, 0},
    {"g_nmp_ttnoisy", (const void*)&g_nmp_ttnoisy, 1, 0},
    {"g_nmp_verif", (const void*)&g_nmp_verif, 1, 1},
    {"g_nmp_verif_depth", (const void*)&g_nmp_verif_depth, 0, 1},
    {"g_node_cache", (const void*)&g_node_cache, 1, 1},
    {"g_np_key_incr", (const void*)&g_np_key_incr, 1, 1},
    {"g_offense_bonus", (const void*)&g_offense_bonus, 0, 0},
    {"g_opp_worse_margin", (const void*)&g_opp_worse_margin, 0, 23},
    {"g_opp_worsening", (const void*)&g_opp_worsening, 1, 1},
    {"g_pawn_hist", (const void*)&g_pawn_hist, 1, 1},
    {"g_pawn_hist_weight", (const void*)&g_pawn_hist_weight, 0, 211},
    {"g_pawn_key_incr", (const void*)&g_pawn_key_incr, 1, 1},
    {"g_pb_base", (const void*)&g_pb_base, 0, 88},
    {"g_pb_ev", (const void*)&g_pb_ev, 0, 144},
    {"g_pb_ev_marg", (const void*)&g_pb_ev_marg, 0, 97},
    {"g_pb_mc", (const void*)&g_pb_mc, 0, 17},
    {"g_pb_mc_cap", (const void*)&g_pb_mc_cap, 0, 229},
    {"g_pb_scale", (const void*)&g_pb_scale, 0, 50},
    {"g_pb_swing", (const void*)&g_pb_swing, 0, 306},
    {"g_pb_swing_marg", (const void*)&g_pb_swing_marg, 0, 136},
    {"g_pb_ttm", (const void*)&g_pb_ttm, 0, 110},
    {"g_postlmr_hist", (const void*)&g_postlmr_hist, 1, 0},
    {"g_postlmr_scale", (const void*)&g_postlmr_scale, 0, 106},
    {"g_prior_bonus", (const void*)&g_prior_bonus, 1, 1},
    {"g_prior_bonus_factor", (const void*)&g_prior_bonus_factor, 1, 0},
    {"g_prior_bonus_gate", (const void*)&g_prior_bonus_gate, 1, 0},
    {"g_prior_bonus_scale", (const void*)&g_prior_bonus_scale, 0, 189},
    {"g_probcut", (const void*)&g_probcut, 1, 1},
    {"g_probcut_depth_off", (const void*)&g_probcut_depth_off, 0, 4},
    {"g_probcut_improve", (const void*)&g_probcut_improve, 0, 4},
    {"g_probcut_improving", (const void*)&g_probcut_improving, 1, 1},
    {"g_probcut_incheck_margin", (const void*)&g_probcut_incheck_margin, 0, 331},
    {"g_probcut_margin", (const void*)&g_probcut_margin, 0, 221},
    {"g_probcut_min_depth", (const void*)&g_probcut_min_depth, 0, 5},
    {"g_probcut_tt", (const void*)&g_probcut_tt, 1, 1},
    {"g_probcut_tt_all", (const void*)&g_probcut_tt_all, 0, 0},
    {"g_promo_qs", (const void*)&g_promo_qs, 0, 6},
    {"g_qfut_margin", (const void*)&g_qfut_margin, 0, 199},
    {"g_qfut_vic_scale", (const void*)&g_qfut_vic_scale, 0, 500},
    {"g_qfutility", (const void*)&g_qfutility, 1, 0},
    {"g_qs_bc_margin", (const void*)&g_qs_bc_margin, 0, 183},
    {"g_qs_cap_recap", (const void*)&g_qs_cap_recap, 0, 0},
    {"g_qs_capthist", (const void*)&g_qs_capthist, 1, 1},
    {"g_qs_capthist_scale", (const void*)&g_qs_capthist_scale, 0, 72},
    {"g_qs_checks", (const void*)&g_qs_checks, 1, 0},
    {"g_qs_delta", (const void*)&g_qs_delta, 0, 1801},
    {"g_qs_delta_bestcase", (const void*)&g_qs_delta_bestcase, 1, 0},
    {"g_qs_draw_check", (const void*)&g_qs_draw_check, 1, 0},
    {"g_qs_move_cap", (const void*)&g_qs_move_cap, 0, 3},
    {"g_qs_stalemate_check", (const void*)&g_qs_stalemate_check, 1, 1},
    {"g_qs_tt_quiets", (const void*)&g_qs_tt_quiets, 0, 0},
    {"g_qsearch_corr", (const void*)&g_qsearch_corr, 1, 1},
    {"g_quiet_offense", (const void*)&g_quiet_offense, 1, 0},
    {"g_razor_base", (const void*)&g_razor_base, 0, 205},
    {"g_razor_depth4", (const void*)&g_razor_depth4, 1, 0},
    {"g_razor_depth_cap", (const void*)&g_razor_depth_cap, 0, 6},
    {"g_razor_mult", (const void*)&g_razor_mult, 0, 108},
    {"g_razor_quad_coef", (const void*)&g_razor_quad_coef, 0, 79},
    {"g_razor_ttlower", (const void*)&g_razor_ttlower, 1, 0},
    {"g_razor_ttquiet", (const void*)&g_razor_ttquiet, 1, 0},
    {"g_recapture_ext", (const void*)&g_recapture_ext, 1, 0},
    {"g_recapture_tension", (const void*)&g_recapture_tension, 0, 70},
    {"g_rfp_badnode", (const void*)&g_rfp_badnode, 0, 4},
    {"g_rfp_depth8", (const void*)&g_rfp_depth8, 1, 0},
    {"g_rfp_depth_cap", (const void*)&g_rfp_depth_cap, 0, 7},
    {"g_rfp_hist_thresh", (const void*)&g_rfp_hist_thresh, 0, 64},
    {"g_rfp_margin", (const void*)&g_rfp_margin, 0, 61},
    {"g_rfp_ttmove_gate", (const void*)&g_rfp_ttmove_gate, 0, 0},
    {"g_see_cap_margin", (const void*)&g_see_cap_margin, 0, 81},
    {"g_see_depth", (const void*)&g_see_depth, 0, 3},
    {"g_see_lmr_depth", (const void*)&g_see_lmr_depth, 1, 0},
    {"g_see_lmr_linear", (const void*)&g_see_lmr_linear, 1, 0},
    {"g_see_lmr_linear_coef", (const void*)&g_see_lmr_linear_coef, 0, 368},
    {"g_see_lmr_prune_cap", (const void*)&g_see_lmr_prune_cap, 0, 27},
    {"g_see_lmr_quiet_margin", (const void*)&g_see_lmr_quiet_margin, 0, 193},
    {"g_see_quiet_margin", (const void*)&g_see_quiet_margin, 0, 104},
    {"g_see_stalemate_guard", (const void*)&g_see_stalemate_guard, 1, 1},
    {"g_seo_mult", (const void*)&g_seo_mult, 0, 27},
    {"g_singular_de_cap", (const void*)&g_singular_de_cap, 0, 1},
    {"g_singular_depth_div", (const void*)&g_singular_depth_div, 0, 2},
    {"g_singular_dmargin", (const void*)&g_singular_dmargin, 0, 62},
    {"g_singular_exact_decouple", (const void*)&g_singular_exact_decouple, 1, 0},
    {"g_singular_exact_margin", (const void*)&g_singular_exact_margin, 1, 0},
    {"g_singular_exact_maxdepth", (const void*)&g_singular_exact_maxdepth, 0, 0},
    {"g_singular_ext", (const void*)&g_singular_ext, 1, 1},
    {"g_singular_mindepth", (const void*)&g_singular_mindepth, 0, 6},
    {"g_singular_mpd", (const void*)&g_singular_mpd, 0, 1},
    {"g_singular_plyguard", (const void*)&g_singular_plyguard, 1, 0},
    {"g_singular_tmargin", (const void*)&g_singular_tmargin, 0, 285},
    {"g_singular_ttmargin", (const void*)&g_singular_ttmargin, 0, 3},
    {"g_statscore_lmr", (const void*)&g_statscore_lmr, 1, 1},
    {"g_tb_tt_store", (const void*)&g_tb_tt_store, 1, 1},
    {"g_threat_hist", (const void*)&g_threat_hist, 1, 1},
    {"g_threat_hist_weight", (const void*)&g_threat_hist_weight, 0, 135},
    {"g_threat_ordering", (const void*)&g_threat_ordering, 1, 1},
    {"g_threat_scale", (const void*)&g_threat_scale, 0, 4212},
    {"g_triple_ext", (const void*)&g_triple_ext, 1, 1},
    {"g_tt_decisive_clamp", (const void*)&g_tt_decisive_clamp, 1, 1},
    {"g_tt_eval_improve", (const void*)&g_tt_eval_improve, 1, 1},
    {"g_tt_faillow_move", (const void*)&g_tt_faillow_move, 0, 1},
    {"g_tt_prefetch", (const void*)&g_tt_prefetch, 1, 1},
    {"g_tt_research", (const void*)&g_tt_research, 1, 0},
    {"g_tt_research_margin", (const void*)&g_tt_research_margin, 0, 74},
    {"g_rdr_knee", (const void*)&g_rdr_knee, 0, 17},
    {"g_rdr_rfp", (const void*)&g_rdr_rfp, 0, 0},
    {"g_rdr_lmp", (const void*)&g_rdr_lmp, 0, 0},
    {"g_rdr_probcut", (const void*)&g_rdr_probcut, 0, 0},
    {"g_tt_nearmiss", (const void*)&g_tt_nearmiss, 0, 100},
    {"g_tt_damp", (const void*)&g_tt_damp, 0, 41},
    {"g_pv_tt_min_depth", (const void*)&g_pv_tt_min_depth, 1, 0},
    {"g_rr_red_pct", (const void*)&g_rr_red_pct, 0, 100},
    {"g_null_threat_ext", (const void*)&g_null_threat_ext, 0, 0},
    {"g_tm_drift", (const void*)&g_tm_drift, 0, 0},
    {"g_tm_drift_thresh", (const void*)&g_tm_drift_thresh, 0, 40},
    {"g_tm_drift_moves", (const void*)&g_tm_drift_moves, 0, 8},
    {"g_tt_static_eval", (const void*)&g_tt_static_eval, 1, 1},
    {"g_ttcut_bonus", (const void*)&g_ttcut_bonus, 1, 1},
    {"g_ttcut_bonus_scale", (const void*)&g_ttcut_bonus_scale, 0, 122},
    {"g_ttcut_depth_override", (const void*)&g_ttcut_depth_override, 0, 0},
    {"g_ttcut_exact", (const void*)&g_ttcut_exact, 0, 0},
    {"g_ttcut_fifty", (const void*)&g_ttcut_fifty, 0, 92},
    {"g_ttcut_malus", (const void*)&g_ttcut_malus, 1, 0},
    {"g_ttcut_malus_seen", (const void*)&g_ttcut_malus_seen, 0, 3},
    {"g_ttcut_refine", (const void*)&g_ttcut_refine, 1, 1},
    {"g_ttpv_amount", (const void*)&g_ttpv_amount, 0, 1},
    {"g_ttpv_inherit", (const void*)&g_ttpv_inherit, 0, 0},
    {"g_upcoming_rep", (const void*)&g_upcoming_rep, 1, 1},
    {"g_wallpawn_penalty", (const void*)&g_wallpawn_penalty, 0, 16800},
};
static const int g_frozen_n = (int)(sizeof(g_frozen_refs) / sizeof(g_frozen_refs[0]));
void triumv_frozen_check();
#define g_aggr_lmr false
#define g_aggr_lmr_clamp 4
#define g_aggr_lmr_div 903
#define g_alpha_depth_dec true
#define g_alpha_depth_dec_amt 1
#define g_bad_noisy true
#define g_bad_noisy_count 7
#define g_badcap_skip_after 1
#define g_brilliant_sac false
#define g_brilliant_sac_lmr false
#define g_brilliant_sac_lmr_amt 2
#define g_brilliant_sac_margin 5000
#define g_cap_futility true
#define g_capfut_base 159
#define g_capfut_chist 362
#define g_capfut_depth 8
#define g_capfut_mult 231
#define g_capfut_vic_scale 570
#define g_caphist_div 21
#define g_caphist_threat false
#define g_capture_hist true
#define g_captured_mailbox 1
#define g_check_bonus 13357
#define g_check_ext_depth 0
#define g_check_ordering true
#define g_cmhc_ply1 0
#define g_cmhc_scale 6
#define g_cont_hist_prune false
#define g_conthist36 true
#define g_conthist36_weight 37
#define g_conthist_lmr true
#define g_conthist_multi false
#define g_corr_major true
#define g_conthist_prune_depth 2
#define g_conthist_red_div 3684
#define g_conthist_weight 144
#define g_corr_asym 137
#define g_corr_cap 48
#define g_corr_cont true
#define g_corr_cont_weight 77
#define g_corr_faillow_all 0
#define g_corr_hist true
#define g_corr_lr_div 303
#define g_corr_material false
#define g_corr_material_weight 67
#define g_corr_trans_weight 0
#define g_corr_multi true
#define g_corr_nonpawn false
#define g_corr_np_weight 100
#define g_corr_tb_guard 1
#define g_corr_uncert true
#define g_corrval_ext true
#define g_corrval_fut 38
#define g_corrval_lmr 83
#define g_corrval_margin true
#define g_corrval_rfp 41
#define g_corrval_see 27
#define g_countermove true
#define g_cu_cap 70
#define g_cu_fut 71
#define g_cu_rfp 60
#define g_cutnode_lmr true
#define g_cutnode_lmr_extra 1
#define g_cutnode_prop 0
#define g_cutoff_stats false
#define g_cutoffcnt_penalty 2
#define g_diverse_smp true
#define g_do_deeper false
#define g_dodeeper_base 43
#define g_doshallower_margin 7
#define g_draw_dither true
#define g_easycap_gate false
#define g_ep_key_fix true
#define g_eval_cache true
#define g_eval_off false
#define g_eval_tt_write false
#define g_evalcache_opt_split false
#define g_evalcache_undamp true
#define g_evasion_gen true
#define g_fast_rep_scan true
#define g_fh_smooth true
#define g_fh_t_qs_final 512
#define g_fh_t_qs_standpat 512
#define g_fh_t_rfp 512
#define g_fhboost_margin 2
#define g_follow_pv false
#define g_fut_base 161
#define g_fut_depth 7
#define g_fut_improving 117
#define g_fut_mult 120
#define g_fut_spare_quiet false
#define g_goodcap_hist_div 32
#define g_goodcap_ttquiet false
#define g_hindsight_ext true
#define g_hindsight_margin 3
#define g_hindsight_red false
#define g_hindsight_red_margin 2
#define g_hindsight_red_thresh 113
#define g_hist_bonus_max 2785
#define g_hist_bonus_mult 471
#define g_hist_bonus_sf true
#define g_hist_bonus_sub 255
#define g_hist_red_div 3190
#define g_hist_triv_guard false
#define g_histprune_margin 2097
#define g_iid false
#define g_iid_depth 4
#define g_iid_reduction 2
#define g_iir_amount 1
#define g_iir_min_depth 4
#define g_iir_no_allnode 0
#define g_iir_not_in_check false
#define g_improving true
#define g_improving_fix true
#define g_killer_lmr_fix true
#define g_killer_reset true
#define g_lazy_eval true
#define g_lmp_base 16
#define g_lmp_check_guard false
#define g_lmp_improving true
#define g_lmp_quad 141
#define g_lmp_scale 35
#define g_lmr_alpha_gap 0
#define g_lmr_alpha_hi 96
#define g_lmr_alpha_lo 64
#define g_lmr_cap_scale 52
#define g_lmr_captures true
#define g_lmr_ch_div 10942
#define g_lmr_check_exempt 0
#define g_lmr_decisive_beta true
#define g_lmr_enrich true
#define g_lmr_enrich_amount 2
#define g_lmr_eval_margin 33
#define g_lmr_expect 0
#define g_lmr_fine true
#define g_lmr_min_depth 3
#define g_lmr_min_moves 1
#define g_lmr_negative 0
#define g_lmr_ss_div 5430
#define g_lmr_ss_offset -2953
#define g_lmr_ttdepth 1
#define g_lmrdepth_histdiv 4509
#define g_lmrdepth_prune 1
#define g_lmrf_all 652
#define g_lmrf_cont4 0
#define g_lmrf_corr 730
#define g_lmrf_cut 3284
#define g_lmrf_cut_nott 2142
#define g_lmrf_cutoff 1199
#define g_lmrf_evalcut 1025
#define g_lmrf_improv 450
#define g_lmrf_killer 801
#define g_lmrf_ply 485
#define g_lmrf_pv 356
#define g_lmrf_ss 616
#define g_lmrf_ttcap 189
#define g_lmrf_ttpv 640
#define g_lowply true
#define g_lowply_weight 179
#define g_mainhist_weight 93
#define g_malus_pct 58
#define g_malus_quad false
#define g_malus_quad_coef 45
#define g_malus_scale_coef 59
#define g_malus_scaled true
#define g_mate_dist true
#define g_move_picker true
#define g_multicut true
#define g_multicut_corr false
#define g_multicut_corr_scale 125
#define g_multicut_ttmalus false
#define g_multicut_ttmalus_scale 100
#define g_nc_bonus 2080
#define g_nc_min_sum 1066
#define g_negext_alpha 2
#define g_negext_cut 3
#define g_negext_order 0
#define g_negext_tt 2
#define g_nmp_base 5
#define g_nmp_cutnode_only 0
#define g_nmp_div 3
#define g_nmp_eval_div 256
#define g_nmp_eval_scale false
#define g_nmp_sm_bias 414
#define g_nmp_sm_mult 23
#define g_nmp_static_margin false
#define g_nmp_ttnoisy false
#define g_nmp_verif true
#define g_nmp_verif_depth 1
#define g_node_cache true
#define g_np_key_incr true
#define g_offense_bonus 0
#define g_opp_worse_margin 23
#define g_opp_worsening true
#define g_pawn_hist true
#define g_pawn_hist_weight 211
#define g_pawn_key_incr true
#define g_pb_base 88
#define g_pb_ev 144
#define g_pb_ev_marg 97
#define g_pb_mc 17
#define g_pb_mc_cap 229
#define g_pb_scale 50
#define g_pb_swing 306
#define g_pb_swing_marg 136
#define g_pb_ttm 110
#define g_postlmr_hist false
#define g_postlmr_scale 106
#define g_prior_bonus true
#define g_prior_bonus_factor false
#define g_prior_bonus_gate false
#define g_prior_bonus_scale 189
#define g_probcut true
#define g_probcut_depth_off 4
#define g_probcut_improve 4
#define g_probcut_improving true
#define g_probcut_incheck_margin 331
#define g_probcut_margin 221
#define g_probcut_min_depth 5
#define g_probcut_tt true
#define g_probcut_tt_all 0
#define g_promo_qs 6
#define g_qfut_margin 199
#define g_qfut_vic_scale 500
#define g_qfutility false
#define g_qs_bc_margin 183
#define g_qs_cap_recap 0
#define g_qs_capthist true
#define g_qs_capthist_scale 72
#define g_qs_checks false
#define g_qs_delta 1801
#define g_qs_delta_bestcase false
#define g_qs_draw_check false
#define g_qs_move_cap 3
#define g_qs_stalemate_check true
#define g_qs_tt_quiets 0
#define g_qsearch_corr true
#define g_quiet_offense false
#define g_razor_base 205
#define g_razor_depth4 false
#define g_razor_depth_cap 6
#define g_razor_mult 108
#define g_razor_quad_coef 79
#define g_razor_ttlower false
#define g_razor_ttquiet false
#define g_recapture_ext false
#define g_recapture_tension 70
#define g_rfp_badnode 4
#define g_rfp_depth8 false
#define g_rfp_depth_cap 7
#define g_rfp_hist_thresh 64
#define g_rfp_margin 61
#define g_rfp_ttmove_gate 0
#define g_see_cap_margin 81
#define g_see_depth 3
#define g_see_lmr_depth false
#define g_see_lmr_linear false
#define g_see_lmr_linear_coef 368
#define g_see_lmr_prune_cap 27
#define g_see_lmr_quiet_margin 193
#define g_see_quiet_margin 104
#define g_see_stalemate_guard true
#define g_seo_mult 27
#define g_singular_de_cap 1
#define g_singular_depth_div 2
#define g_singular_dmargin 62
#define g_singular_exact_decouple false
#define g_singular_exact_margin false
#define g_singular_exact_maxdepth 0
#define g_singular_ext true
#define g_singular_mindepth 6
#define g_singular_mpd 1
#define g_singular_plyguard false
#define g_singular_tmargin 285
#define g_singular_ttmargin 3
#define g_statscore_lmr true
#define g_tb_tt_store true
#define g_threat_hist true
#define g_threat_hist_weight 135
#define g_threat_ordering true
#define g_threat_scale 4212
#define g_triple_ext true
#define g_tt_decisive_clamp true
#define g_tt_eval_improve true
#define g_tt_faillow_move 1
#define g_tt_prefetch true
#define g_tt_research false
#define g_tt_research_margin 74
#define g_rdr_knee 17
#define g_rdr_rfp 0
#define g_rdr_lmp 0
#define g_rdr_probcut 0
#define g_tt_nearmiss 100
#define g_tt_damp 41
#define g_pv_tt_min_depth false
#define g_rr_red_pct 100
#define g_null_threat_ext 0
#define g_tm_drift 0
#define g_tm_drift_thresh 40
#define g_tm_drift_moves 8
#define g_tt_static_eval true
#define g_ttcut_bonus true
#define g_ttcut_bonus_scale 122
#define g_ttcut_depth_override 0
#define g_ttcut_exact 0
#define g_ttcut_fifty 92
#define g_ttcut_malus false
#define g_ttcut_malus_seen 3
#define g_ttcut_refine true
#define g_ttpv_amount 1
#define g_ttpv_inherit 0
#define g_upcoming_rep true
#define g_wallpawn_penalty 16800
#endif  // TRIUMV_FROZEN
// =======================================================================
