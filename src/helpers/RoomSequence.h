#pragma once
#include <stdint.h>

// RFC-1982-style serial arithmetic for the Room post sequence counter.
// Correct when compared values are less than 2^31 increments apart, which
// is guaranteed by the bounded journal window.
static inline bool roomSequenceIsNewer(uint32_t candidate, uint32_t current) {
  const uint32_t delta = candidate - current;
  return delta != 0 && delta < 0x80000000UL;
}
