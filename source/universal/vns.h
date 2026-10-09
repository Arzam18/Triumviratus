// Binario universale a file separati (09/10/2026, build_universal.ps1 -MultiTU): namespace e sezione dei costruttori
// globali della variante -DTRIUMV_VID=<n>, condivisi da tutti gli involucri della variante (un involucro per ogni .cpp
// del motore, come le unita' della build separata). Stesse regole di variant.cpp: i costruttori vanno in .tv<n>$m e
// li esegue entry.cpp (o standalone.cpp) solo per la variante scelta.
#pragma once

#if TRIUMV_VID == 1
#pragma init_seg(".tv1$m")
#define TRIUMV_VNS Triumv_v1
#elif TRIUMV_VID == 2
#pragma init_seg(".tv2$m")
#define TRIUMV_VNS Triumv_v2
#elif TRIUMV_VID == 3
#pragma init_seg(".tv3$m")
#define TRIUMV_VNS Triumv_v3
#elif TRIUMV_VID == 4
#pragma init_seg(".tv4$m")
#define TRIUMV_VNS Triumv_v4
#elif TRIUMV_VID == 5
#pragma init_seg(".tv5$m")
#define TRIUMV_VNS Triumv_v5
#else
#error "TRIUMV_VID mancante (1..5)"
#endif
