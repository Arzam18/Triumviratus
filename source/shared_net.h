// shared_net.h -- rete NNUE in memoria condivisa fra processi (01/10/2026), solo con -DTRIUMV_SHARED_NET.
//
// Perche'. Con molti motori sulla stessa macchina (match a 76, SPSA) ogni processo teneva la SUA copia dei pesi:
// circa 245 MB l'uno, 18 GB a 76 motori, e soprattutto 19 copie diverse degli stessi dati in concorrenza per la
// stessa L3 di socket. Misurato il 01/10: un motore perde il 46% di NPS con 18 altri motori sul socket. Con la rete
// condivisa i processi leggono le STESSE pagine fisiche, quindi le righe calde della rete si dividono la cache invece
// di sfrattarsi a vicenda, e la RAM scende a una copia per nodo NUMA.
//
// Come fa Stockfish (src/shm.h, SystemWideSharedConstant): il primo processo che carica una rete crea un'area con
// nome (CreateFileMapping su pagefile), ci copia l'oggetto Network intero (e' copiabile byte per byte: pesi in array,
// nomi in FixedString) e la segna pronta; gli altri la aprono in SOLA LETTURA e liberano la propria copia.
// Il nome contiene l'hash del contenuto della rete, la sua dimensione e il nodo NUMA del processo: reti diverse o
// layout diversi non si mescolano mai, e ogni socket ha la sua copia nella propria memoria (la crea il primo processo
// di quel nodo, che la tocca per primo). Large pages se possibile, poi pagine normali, poi la copia locale di sempre.
// Tutto sotto un mutex con nome: un processo che muore a meta' copia lascia "ready" a zero e gli altri non la usano.
#pragma once
#if defined(TRIUMV_SHARED_NET) && defined(_WIN32)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

#include "nnue/memory.h"

#ifndef FILE_MAP_LARGE_PAGES
    #define FILE_MAP_LARGE_PAGES 0x20000000
#endif
#ifndef SEC_LARGE_PAGES
    #define SEC_LARGE_PAGES 0x80000000
#endif

namespace TriumvShm {

struct Header {
    std::uint64_t magic;
    std::uint64_t hash;
    std::uint64_t size;
    std::uint32_t largePages;
    volatile LONG ready;
};
constexpr std::uint64_t Magic       = 0x5452494D56534E31ull;   // "TRIMVSN1"
constexpr std::size_t   HeaderBytes = 4096;                    // i pesi partono allineati a pagina

struct View {
    HANDLE map  = nullptr;
    void*  base = nullptr;
};
inline View& current() { static View v; return v; }

inline int numa_node() {
    PROCESSOR_NUMBER pn{};
    GetCurrentProcessorNumberEx(&pn);
    USHORT node = 0;
    return GetNumaProcessorNodeEx(&pn, &node) ? int(node) : 0;
}

// Rilascia la vista precedente (ricaricamento della rete). L'area sparisce quando l'ultimo processo la chiude.
inline void release() {
    View& v = current();
    if (v.base) UnmapViewOfFile(v.base);
    if (v.map) CloseHandle(v.map);
    v = View{};
}

// Apre in sola lettura un'area gia' esistente (prima come large pages, poi normale) e controlla la testata.
// 1 = aperta e pronta; 0 = non esiste; -1 = esiste ma non e' (ancora) pronta o non corrisponde.
// Si puo' chiamare anche senza il mutex: chi crea scrive `ready` per ultimo, dopo la barriera.
inline int open_ready(const char* name, std::uint64_t hash, std::size_t size, int node, View& v, const void*& result,
                      std::string& status) {
    if ((v.map = OpenFileMappingA(FILE_MAP_READ, FALSE, name)) == nullptr)
        return 0;
    v.base = MapViewOfFile(v.map, FILE_MAP_READ | FILE_MAP_LARGE_PAGES, 0, 0, 0);
    if (!v.base) v.base = MapViewOfFile(v.map, FILE_MAP_READ, 0, 0, 0);
    const Header* h = static_cast<const Header*>(v.base);
    if (h && h->magic == Magic && h->hash == hash && h->size == size && h->ready == 1) {
        result = static_cast<const char*>(v.base) + HeaderBytes;
        status = std::string("shared (opened") + (h->largePages ? ", large pages" : "") + ", node "
               + std::to_string(node) + ")";
        return 1;
    }
    if (v.base) UnmapViewOfFile(v.base);
    CloseHandle(v.map);
    v = View{};
    return -1;
}

// Ritorna un puntatore all'oggetto condiviso (sola lettura per chi lo apre, scritto una volta da chi lo crea),
// oppure nullptr: allora il chiamante tiene la sua copia locale. `status` descrive l'esito per l'info string.
inline const void* attach(const void* src, std::size_t size, std::uint64_t hash, std::string& status) {
    release();
    const int node = numa_node();
    char name[128];
    std::snprintf(name, sizeof name, "Local\\triumv_net_%016llx_%llx_n%d", (unsigned long long) hash,
                  (unsigned long long) size, node);
    const std::string mtxName = std::string(name) + "_mtx";

    HANDLE mtx = CreateMutexA(nullptr, FALSE, mtxName.c_str());
    if (!mtx) { status = "local (mutex)"; return nullptr; }

    const std::size_t total = HeaderBytes + size;
    View v;
    const void* result = nullptr;

    // Attesa del mutex senza arrendersi (08/10/2026). Prima, dopo 120 s, il processo teneva la sua copia privata.
    // Chi crea l'area tiene il mutex anche mentre il sistema cerca le large pages, e su un nodo con la memoria
    // frammentata da ore di match questo puo' durare minuti: nell'SPRT del 08/10 (6+0.06, 140 motori) 9 motori
    // sono rimasti con una copia privata (402 MB invece di 175), tutti avviati nello stesso secondo, e il socket 1
    // ha dato -20,8 +- 3,6 Elo contro -0,1 +- 3,2 del socket 0 con la stessa rete, costante dall'inizio alla fine.
    // Ora si aspetta a passi di 1 s e a ogni passo si prova ad aprire l'area gia' pronta senza mutex; la copia
    // privata resta solo dopo 15 minuti.
    DWORD w = WAIT_TIMEOUT;
    for (int s = 0; s < 900 && w == WAIT_TIMEOUT; ++s) {
        w = WaitForSingleObject(mtx, 1000);
        if (w == WAIT_TIMEOUT && open_ready(name, hash, size, node, v, result, status) == 1) {
            CloseHandle(mtx);
            current() = v;
            return result;
        }
    }
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) { CloseHandle(mtx); status = "local (mutex timeout)"; return nullptr; }

    // 1) L'area esiste gia': la apre in sola lettura.
    const int op = open_ready(name, hash, size, node, v, result, status);
    if (op == -1) {
        status = "local (shared area not ready)";
    } else if (op == 0) {
        // 2) Non esiste: la crea, prima con large pages (serve SeLockMemoryPrivilege), poi con pagine normali.
        DWORD lpErr = 0;   // perche' le large pages non sono riuscite (0 = privilegio non ottenuto)
        bool large = Triumviratus::windows_try_with_large_page_priviliges(
          [&](std::size_t lp) {
              const std::size_t sz = (total + lp - 1) / lp * lp;
              v.map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,
                                         DWORD(std::uint64_t(sz) >> 32), DWORD(sz & 0xFFFFFFFFu), name);
              if (!v.map) { lpErr = GetLastError(); return false; }
              v.base = MapViewOfFile(v.map, FILE_MAP_READ | FILE_MAP_WRITE | FILE_MAP_LARGE_PAGES, 0, 0, 0);
              if (!v.base) { lpErr = 100000 + GetLastError(); CloseHandle(v.map); v.map = nullptr; return false; }
              return true;
          },
          [] { return false; });
        if (!large) {
            v.map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT,
                                       DWORD(std::uint64_t(total) >> 32), DWORD(total & 0xFFFFFFFFu), name);
            if (v.map) v.base = MapViewOfFile(v.map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
        }
        if (v.base) {
            Header* h = static_cast<Header*>(v.base);
            std::memcpy(static_cast<char*>(v.base) + HeaderBytes, src, size);   // tocca le pagine: nodo locale
            h->magic = Magic; h->hash = hash; h->size = size; h->largePages = large ? 1 : 0;
            MemoryBarrier();
            h->ready = 1;
            result = static_cast<const char*>(v.base) + HeaderBytes;
            status = std::string("shared (created") + (large ? ", large pages" : ", no large pages: err "
                   + std::to_string(lpErr)) + ", node " + std::to_string(node) + ")";
        } else {
            if (v.map) CloseHandle(v.map);
            v = View{};
            status = "local (cannot create shared area)";
        }
    }
    ReleaseMutex(mtx);
    CloseHandle(mtx);
    if (result) current() = v;
    return result;
}

}  // namespace TriumvShm

#endif
