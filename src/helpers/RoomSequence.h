#pragma once
#include <stdint.h>

// RFC-1982-style serial arithmetic for the Room post sequence counter.
// Comparisons are valid when values are less than 2^31 increments apart,
// which is guaranteed by the bounded Room journal.
static inline bool roomSequenceIsNewer(uint32_t candidate, uint32_t current) {
  const uint32_t delta = candidate - current;
  return delta != 0 && delta < 0x80000000UL;
}

// Tracks the newest sequence in journal order. The first record initializes
// the tracker even when its value is above 0x7FFFFFFF.
class RoomSequenceTracker {
  bool _has_value;
  uint32_t _value;
public:
  RoomSequenceTracker() : _has_value(false), _value(0) {}
  void observe(uint32_t candidate) {
    if (!_has_value || roomSequenceIsNewer(candidate, _value)) {
      _value = candidate;
      _has_value = true;
    }
  }
  bool hasValue() const { return _has_value; }
  uint32_t value() const { return _value; }
};
