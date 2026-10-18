// redlog.h -- data collection for block 2 of 8.1 (only with -DTRIUMV_REDLOG; never in a release).
// Every RedlogEvery-th move that reaches the late-move code writes one record: the features of the node and the
// move and the reduction the current formula gives it. tools/redfit.py fits our own reduction formula on them.
// Files: redlog_<thread>.bin in the working directory, appended.
#pragma once
#include <cstdint>
#include <cstdio>

#ifndef REDLOG_EVERY
#define REDLOG_EVERY 8
#endif

#pragma pack(push, 1)
struct RedlogRec {
  int16_t depth, moves, move_no, expert;
  int32_t stat_score;
  int16_t gap, cutoff_cnt;
  int32_t r_entry, r_final, signal;
  int16_t window_term, ply;
  uint16_t flags;
};
#pragma pack(pop)

static inline void redlog_write(int tid, int depth, int moves, int move_no, int expert, int stat_score, int gap,
                                int cutoff_cnt, int r_entry, int r_final, int signal, int window_term, int ply,
                                int flags) {
  static FILE *files[1024];
  static unsigned counters[1024 * 16];   // one cache line per thread
  if (tid < 0 || tid >= 1024 || ++counters[tid * 16] % REDLOG_EVERY)
    return;
  FILE *&fp = files[tid];
  if (!fp) {
    char name[64];
    std::snprintf(name, sizeof name, "redlog_%d.bin", tid);
    fp = std::fopen(name, "ab");
    if (!fp)
      return;
  }
  RedlogRec r{(int16_t)depth, (int16_t)moves, (int16_t)move_no, (int16_t)expert, stat_score, (int16_t)gap,
              (int16_t)cutoff_cnt, r_entry, r_final, signal, (int16_t)window_term, (int16_t)ply, (uint16_t)flags};
  std::fwrite(&r, sizeof r, 1, fp);
}
