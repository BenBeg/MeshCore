#include <gtest/gtest.h>
#include "../../src/helpers/AtomicSlotCore.h"

TEST(AtomicSlotRecovery, SelectsOnlyValidSlot) {
  EXPECT_EQ(0, atomicSlotNewest(true, 4, false, 9));
  EXPECT_EQ(1, atomicSlotNewest(false, 4, true, 9));
  EXPECT_EQ(0, atomicSlotNewest(false, 4, false, 9));
}

TEST(AtomicSlotRecovery, NewestGenerationAndEqualTie) {
  EXPECT_EQ(1, atomicSlotNewest(true, 4, true, 5));
  EXPECT_EQ(0, atomicSlotNewest(true, 5, true, 5));
  EXPECT_EQ(0, atomicSlotNewest(true, 5, true, 4));
  // An exact half-range difference is ambiguous; deterministic tie policy is slot 0.
  EXPECT_EQ(0, atomicSlotNewest(true, 0, true, 0x80000000UL));
}

TEST(AtomicSlotRecovery, GenerationWraparound) {
  EXPECT_EQ(1, atomicSlotNewest(true, 0xFFFFFFFEUL, true, 1));
  EXPECT_EQ(0, atomicSlotNewest(true, 1, true, 0xFFFFFFFEUL));
}

TEST(AtomicSlotRecovery, FallsBackWhenNewestSecondReadFails) {
  uint8_t attempts[2] = {0, 0};
  bool ok = atomicSlotLoadWithFallback(true, true, 1, [&](uint8_t slot) {
    attempts[slot]++;
    return slot == 0;
  });
  EXPECT_TRUE(ok);
  EXPECT_EQ(1, attempts[1]);
  EXPECT_EQ(1, attempts[0]);
}

TEST(AtomicSlotRecovery, DoesNotAttemptInvalidFallback) {
  uint8_t attempts = 0;
  bool ok = atomicSlotLoadWithFallback(true, false, 0, [&](uint8_t) {
    attempts++;
    return false;
  });
  EXPECT_FALSE(ok);
  EXPECT_EQ(1, attempts);
}

TEST(AtomicSlotRecovery, BothSlotsInvalid) {
  uint8_t attempts = 0;
  bool ok = atomicSlotLoadWithFallback(false, false, 0, [&](uint8_t) {
    attempts++;
    return true;
  });
  EXPECT_FALSE(ok);
  EXPECT_EQ(0, attempts);
}
