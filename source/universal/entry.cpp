// Binario universale (08/10/2026): ingresso. Compilato SENZA istruzioni oltre x86-64 di base, sceglie con cpuid la
// variante migliore che la CPU e il sistema operativo reggono, esegue SOLO i suoi costruttori globali (sezione
// .tv<n>) e chiama il suo main. Varianti (TRIUMV_VID in variant.cpp):
//   5 avx512icl   AVX-512 F/BW/DQ/VL + VNNI + VBMI/VBMI2/BITALG   Ice Lake, Tiger Lake, Sapphire Rapids, Zen 4/5
//   4 vnni512     AVX-512 F/BW/DQ/VL + VNNI                       Cascade Lake, Cooper Lake
//   3 avx512      AVX-512 F/BW/DQ/VL                              Skylake-X, Skylake-SP
//   2 avx2        AVX2 + BMI2 (PEXT veloce)                       Intel da Haswell, AMD da Zen 3
//   1 avx2-nopext AVX2 senza PEXT                                 AMD Excavator, Zen 1/Zen+/Zen 2 (PEXT microcodificato)
// La variabile d'ambiente TRIUMV_ISA (avx2-nopext, avx2, avx512, vnni512, avx512icl) forza una variante, ma solo se la
// CPU la regge: serve alle misure (stesso binario, varianti diverse) e alle prove.
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void(__cdecl *TvInit)(void);

#define TV_DECLARE(n)                                                                                               \
  namespace Triumv_v##n { int main(); }                                                                             \
  __pragma(section(".tv" #n "$a", read)) __pragma(section(".tv" #n "$z", read))                                    \
  __declspec(allocate(".tv" #n "$a")) const TvInit tv##n##_a = nullptr;                                             \
  __declspec(allocate(".tv" #n "$z")) const TvInit tv##n##_z = nullptr;                                             \
  static int run_v##n() {                                                                                           \
    for (const TvInit *p = &tv##n##_a + 1; p < &tv##n##_z; ++p)                                                    \
      if (*p)                                                                                                       \
        (*p)();                                                                                                     \
    return Triumv_v##n::main();                                                                                     \
  }

TV_DECLARE(1)
TV_DECLARE(2)
TV_DECLARE(3)
TV_DECLARE(4)
TV_DECLARE(5)

static const char *const kNames[6] = {"", "avx2-nopext", "avx2", "avx512", "vnni512", "avx512icl"};

// Nome della variante scelta, letto dal motore per la riga "id name" (extern "C": stesso simbolo per tutte).
extern "C" const char *g_triumv_isa = "";

static int best_variant() {
  int r[4];
  __cpuid(r, 0);
  const int max_leaf = r[0];
  char vendor[13];
  memcpy(vendor, &r[1], 4);
  memcpy(vendor + 4, &r[3], 4);
  memcpy(vendor + 8, &r[2], 4);
  vendor[12] = 0;
  __cpuid(r, 1);
  const unsigned ecx1 = (unsigned)r[2];
  const unsigned eax1 = (unsigned)r[0];
  const int family = ((eax1 >> 8) & 0xF) + (((eax1 >> 8) & 0xF) == 0xF ? ((eax1 >> 20) & 0xFF) : 0);
  const bool popcnt = ecx1 & (1u << 23), osxsave = ecx1 & (1u << 27), avx = ecx1 & (1u << 28),
             fma = ecx1 & (1u << 12);
  if (!(popcnt && osxsave && avx && fma) || max_leaf < 7)
    return 0;
  const unsigned long long xcr0 = _xgetbv(0);
  if ((xcr0 & 0x6) != 0x6)   // stato SSE e AVX salvato dal sistema operativo
    return 0;
  __cpuidex(r, 7, 0);
  const unsigned ebx7 = (unsigned)r[1], ecx7 = (unsigned)r[2];
  const bool avx2 = ebx7 & (1u << 5), bmi1 = ebx7 & (1u << 3), bmi2 = ebx7 & (1u << 8);
  if (!(avx2 && bmi1))
    return 0;
  const bool os_zmm = (xcr0 & 0xE0) == 0xE0;   // opmask e registri ZMM salvati dal sistema operativo
  const bool f = ebx7 & (1u << 16), dq = ebx7 & (1u << 17), bw = ebx7 & (1u << 30), vl = ebx7 & (1u << 31);
  const bool vbmi = ecx7 & (1u << 1), vbmi2 = ecx7 & (1u << 6), vnni = ecx7 & (1u << 11), bitalg = ecx7 & (1u << 12);
  if (os_zmm && f && dq && bw && vl && bmi2) {
    if (vnni && vbmi && vbmi2 && bitalg)
      return 5;
    return vnni ? 4 : 3;
  }
  // PEXT/PDEP microcodificati: AMD famiglia 15h (Excavator), 17h (Zen 1/Zen+/Zen 2) e Hygon 18h.
  const bool amd_like = !strcmp(vendor, "AuthenticAMD") || !strcmp(vendor, "HygonGenuine");
  const bool slow_pext = amd_like && (family == 0x15 || family == 0x17 || family == 0x18);
  return bmi2 && !slow_pext ? 2 : 1;
}

int main() {
  const int best = best_variant();
  if (!best) {
    printf("Triumviratus 8.0 needs a CPU with AVX2, BMI1, FMA and POPCNT, and an operating system that saves the AVX "
           "state.\n");
    return 1;
  }
  int v = best;
  if (const char *force = getenv("TRIUMV_ISA")) {
    for (int i = 1; i <= 5; i++)
      if (!strcmp(force, kNames[i])) {
        // Una variante piu' bassa va sempre bene: ogni CPU con AVX-512 ha anche BMI2.
        if (i <= best)
          v = i;
        else
          fprintf(stderr, "TRIUMV_ISA=%s non supportata da questa CPU: uso %s\n", force, kNames[best]);
      }
  }
  g_triumv_isa = kNames[v];
  switch (v) {
  case 1: return run_v1();
  case 2: return run_v2();
  case 3: return run_v3();
  case 4: return run_v4();
  default: return run_v5();
  }
}
