// shared_net.h -- NNUE network in memory shared between processes (01/10/2026), only with -DTRIUMV_SHARED_NET.
//
// Why. With many engines on the same machine (matches with 76, SPSA) every process kept ITS OWN copy of the weights:
// about 245 MB each, 18 GB with 76 engines, and above all 19 different copies of the same data competing for the
// same socket L3. Measured on 01/10: an engine loses 46% of NPS with 18 other engines on the socket. With the shared
// network the processes read the SAME physical pages, so the hot rows of the network share the cache instead
// of evicting each other, and the RAM drops to one copy per NUMA node.
//
// The first process that loads a network creates a named
// area (CreateFileMapping on the pagefile), copies into it the whole Network object (it is byte-copyable: weights in arrays,
// names in FixedString) and marks it ready; the others open it READ-ONLY and free their own copy.
// The name contains the hash of the network's content, its size and the process's NUMA node: different networks or
// different layouts never mix, and every socket has its copy in its own memory (created by the first process
// of that node, which touches it first). Large pages if possible, then normal pages, then the usual local copy.
// All under a named mutex: a process dying mid-copy leaves "ready" at zero and the others do not use it.
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

#include "large_pages.h"

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

// Releases the previous view (network reload). The area disappears when the last process closes it.
inline void release() {
    View& v = current();
    if (v.base) UnmapViewOfFile(v.base);
    if (v.map) CloseHandle(v.map);
    v = View{};
}

// Opens read-only an already existing area (first as large pages, then normal) and checks the header.
// 1 = open and ready; 0 = does not exist; -1 = exists but is not (yet) ready or does not match.
// It can be called without the mutex too: the creator writes `ready` last, after the barrier.
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

// Returns a pointer to the shared object (read-only for whoever opens it, written once by whoever creates it),
// or nullptr: then the caller keeps its local copy. `status` describes the outcome for the info string.
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

    // Waiting for the mutex without giving up (08/10/2026). Before, after 120 s, the process kept its private copy.
    // Whoever creates the area holds the mutex also while the system looks for large pages, and on a node with memory
    // fragmented by hours of matches this can take minutes: in the SPRT of 08/10 (6+0.06, 140 engines) 9 engines
    // were left with a private copy (402 MB instead of 175), all started in the same second, and socket 1
    // gave -20.8 +- 3.6 Elo against -0.1 +- 3.2 for socket 0 with the same network, constant from start to end.
    // Now it waits in steps of 1 s and at every step tries to open the already ready area without the mutex; the private
    // copy remains only after 15 minutes.
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

    // 1) The area already exists: it opens it read-only.
    const int op = open_ready(name, hash, size, node, v, result, status);
    if (op == -1) {
        status = "local (shared area not ready)";
    } else if (op == 0) {
        // 2) It does not exist: it creates it, first with large pages (needs SeLockMemoryPrivilege), then with normal pages.
        DWORD lpErr = 0;   // why the large pages failed (0 = privilege not obtained)
        bool large = with_lock_memory_privilege(
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
            std::memcpy(static_cast<char*>(v.base) + HeaderBytes, src, size);   // touches the pages: local node
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
