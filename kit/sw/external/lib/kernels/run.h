#ifndef LAB_RUN_H
#define LAB_RUN_H

#include <stdint.h>

#define SMALL   0
#define LARGE   1
#define RUN_ALL 0

#include "config.h"

#define ISOLATED    ((RUN) != RUN_ALL)
#define RUNS(calls) (!ISOLATED || ((RUN) & (calls)))

#ifdef __riscv
#include "vcd_dump.h"
#else
static inline void dump_on(void) {}
static inline void dump_off(void) {}
#endif

// Deterministic pseudo-random bytes: the inputs of an isolated run.
static inline void fill_rand(void *p, int nbytes, uint32_t seed) {
    uint8_t *b = (uint8_t *)p;
    for (int i = 0; i < nbytes; i++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        b[i] = (uint8_t)seed;
    }
}

// FNV-1a over a buffer, chained through h.
static inline uint32_t checksum_bytes(uint32_t h, const void *p, int nbytes) {
    const uint8_t *b = (const uint8_t *)p;
    for (int i = 0; i < nbytes; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

#define CHECKSUM_INIT 2166136261u

#endif
