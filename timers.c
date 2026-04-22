#define _CRT_SECURE_NO_WARNINGS

#include <time.h>

#include "timers.h"

static unsigned int v_timers[MAX_TIMERS];
static clock_t last_time = 0;

void InitTimers(void) {
  for (int i = 0; i < MAX_TIMERS; i++) {
    v_timers[i] = 0;
  }
  last_time = clock();
}

unsigned int GetTimer(int Timer) {
  clock_t now = clock();
  clock_t diff = (now - last_time) * 1000 / CLOCKS_PER_SEC;
  if (diff > 0) {
    for (int i = 0; i < MAX_TIMERS; i++) {
      v_timers[i] += (unsigned int)diff;
    }
    last_time = now;
  }
  return v_timers[Timer];
}

void ResetTimer(int Timer) { v_timers[Timer] = 0; }