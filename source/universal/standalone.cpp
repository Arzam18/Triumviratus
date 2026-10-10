// Binario universale a file separati (09/10/2026, build_universal.ps1 -MultiTU): main della sola variante, per le build
// strumentate del training PGO (al posto di entry.cpp). Stesso codice del blocco TRIUMV_STANDALONE di variant.cpp: i
// costruttori globali della variante stanno in .tv<n>$m e il CRT non li esegue, li esegue questo main.
#include "vns.h"

namespace TRIUMV_VNS { int main(); }

typedef void(__cdecl *TvInit)(void);
#if TRIUMV_VID == 1
#pragma section(".tv1$a", read)
#pragma section(".tv1$z", read)
__declspec(allocate(".tv1$a")) const TvInit tv_a = nullptr;
__declspec(allocate(".tv1$z")) const TvInit tv_z = nullptr;
#elif TRIUMV_VID == 2
#pragma section(".tv2$a", read)
#pragma section(".tv2$z", read)
__declspec(allocate(".tv2$a")) const TvInit tv_a = nullptr;
__declspec(allocate(".tv2$z")) const TvInit tv_z = nullptr;
#elif TRIUMV_VID == 3
#pragma section(".tv3$a", read)
#pragma section(".tv3$z", read)
__declspec(allocate(".tv3$a")) const TvInit tv_a = nullptr;
__declspec(allocate(".tv3$z")) const TvInit tv_z = nullptr;
#elif TRIUMV_VID == 4
#pragma section(".tv4$a", read)
#pragma section(".tv4$z", read)
__declspec(allocate(".tv4$a")) const TvInit tv_a = nullptr;
__declspec(allocate(".tv4$z")) const TvInit tv_z = nullptr;
#else
#pragma section(".tv5$a", read)
#pragma section(".tv5$z", read)
__declspec(allocate(".tv5$a")) const TvInit tv_a = nullptr;
__declspec(allocate(".tv5$z")) const TvInit tv_z = nullptr;
#endif
extern "C" const char *g_triumv_isa = "standalone";
int main() {
  for (const TvInit *p = &tv_a + 1; p < &tv_z; ++p)
    if (*p)
      (*p)();
  return TRIUMV_VNS::main();
}
