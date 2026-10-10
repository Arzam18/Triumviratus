// nneurons.h -- studio dei neuroni del primo strato della rete (09/10/2026), SOLO con -DTRIUMV_NEURONS.
// Per ogni valutazione vera della ricerca registra le uscite della trasformazione (clipped ReLU e prodotto a coppie:
// uscita j = clamp(acc[j]) * clamp(acc[j + L1/2]) / 512, per le due prospettive) separate per fascia di materiale,
// cioe' per esperto di HalfKAv2_hm_P4 (pezzi sulla scacchiera, re compresi: <=9, 10-15, 16-23, >=24):
//  - statistiche per uscita: valutazioni con uscita diversa da zero, somma, somma dei quadrati, uscite al massimo;
//  - un campione dei vettori interi (uno ogni NEURONS_EVERY valutazioni, al massimo NEURONS_MAX per fascia) con
//    numero di pezzi, differenza di materiale dal lato al tratto e case dei re, per correlazioni e doppioni.
// Uscita nella cartella della variabile d'ambiente NEURONS_DIR (nulla se non e' impostata): stats_b<k>.txt e
// sample_b<k>.bin (record: 8 byte di intestazione + le uscite). Analisi: tools/neurons_analyze.py.
// Pensato per un thread (Threads 1): i contatori non sono atomici. Senza TRIUMV_NEURONS: codice identico.
#pragma once

#ifdef TRIUMV_NEURONS
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace NNeurons {
constexpr int Bands = 4, MaxOut = 4096;
inline std::uint64_t evals[Bands];
inline std::uint64_t nz[Bands][MaxOut], sum[Bands][MaxOut], sumsq[Bands][MaxOut], sat[Bands][MaxOut];
inline int           width = 0;
inline std::uint64_t kept[Bands];
inline std::FILE*    sample[Bands];
inline std::FILE*    meta[Bands];   // posizioni dei record del campione (record piu' sotto)
inline long          every = -1, maxkeep = 0;
inline std::string   dir;

inline int band_of_count(int pieces) { return pieces <= 9 ? 0 : pieces <= 15 ? 1 : pieces <= 23 ? 2 : 3; }

inline void dump() {
    if (dir.empty() || !width) return;
    for (int b = 0; b < Bands; b++) {
        if (sample[b]) std::fclose(sample[b]);
        if (meta[b]) std::fclose(meta[b]);
        std::FILE* o = std::fopen((dir + "/stats_b" + std::to_string(b) + ".txt").c_str(), "w");
        if (!o) continue;
        std::fprintf(o, "evals %llu width %d\n", (unsigned long long) evals[b], width);
        for (int j = 0; j < width; j++)
            std::fprintf(o, "%d %llu %llu %llu %llu\n", j, (unsigned long long) nz[b][j], (unsigned long long) sum[b][j],
                         (unsigned long long) sumsq[b][j], (unsigned long long) sat[b][j]);
        std::fclose(o);
    }
}

inline void init() {
    const char* d = std::getenv("NEURONS_DIR");
    every   = 0;
    if (!d) return;
    dir     = d;
    const char* e = std::getenv("NEURONS_EVERY");
    const char* m = std::getenv("NEURONS_MAX");
    every   = e ? std::atol(e) : 16;
    maxkeep = m ? std::atol(m) : 100000;
    for (int b = 0; b < Bands; b++)
        sample[b] = std::fopen((dir + "/sample_b" + std::to_string(b) + ".bin").c_str(), "wb");
    std::atexit(dump);
}

// out: le uscite della trasformazione (n byte, n = 2 * L1/2); pieces, matdiff (centesimi di pedone dal lato al tratto),
// stm, ksq_us, ksq_them: dalla posizione valutata.
// bbs (09/10/2026 sera, per il raggruppamento delle uscite e la ricerca di concetti): i dodici bitboard per pezzo
// della posizione (P N B R Q K p n b r q k, case a8 = 0 .. h1 = 63), scritti in meta_b<k>.bin accanto a ogni record
// del campione (104 byte: 12 x 8 byte, lato al tratto, 7 byte a zero), cosi' l'analisi ricostruisce la posizione.
inline void record(const std::uint8_t* out, int n, int pieces, int matdiff, int stm, int ksq_us, int ksq_them,
                   const unsigned long long* bbs = nullptr) {
    if (every < 0) init();
    if (dir.empty()) return;
    width        = n;
    const int b  = band_of_count(pieces);
    const auto k = evals[b]++;
    for (int j = 0; j < n; j++) {
        const unsigned v = out[j];
        nz[b][j] += v != 0;
        sum[b][j] += v;
        sumsq[b][j] += v * v;
        sat[b][j] += v >= 127;
    }
    if (sample[b] && every > 0 && k % every == 0 && kept[b] < std::uint64_t(maxkeep)) {
        const std::int16_t md = std::int16_t(matdiff);
        const std::uint8_t h[8] = {std::uint8_t(pieces), std::uint8_t(stm), std::uint8_t(ksq_us), std::uint8_t(ksq_them),
                                   std::uint8_t(md & 0xff), std::uint8_t((md >> 8) & 0xff), 0, 0};
        std::fwrite(h, 1, 8, sample[b]);
        std::fwrite(out, 1, n, sample[b]);
        if (bbs) {
            if (!meta[b]) meta[b] = std::fopen((dir + "/meta_b" + std::to_string(b) + ".bin").c_str(), "wb");
            if (meta[b]) {
                const std::uint8_t tail[8] = {std::uint8_t(stm), 0, 0, 0, 0, 0, 0, 0};
                std::fwrite(bbs, 8, 12, meta[b]);
                std::fwrite(tail, 1, 8, meta[b]);
            }
        }
        kept[b]++;
    }
}
}  // namespace NNeurons
#endif
