#include <unity.h>
#include "../../src/helpers/AtomicSlotCore.h"

void test_selects_only_valid_slot() {
  TEST_ASSERT_EQUAL_UINT8(0, atomicSlotNewest(true, 4, false, 9));
  TEST_ASSERT_EQUAL_UINT8(1, atomicSlotNewest(false, 4, true, 9));
  TEST_ASSERT_EQUAL_UINT8(0, atomicSlotNewest(false, 4, false, 9));
}
void test_newest_generation_and_equal_tie() {
  TEST_ASSERT_EQUAL_UINT8(1, atomicSlotNewest(true, 4, true, 5));
  TEST_ASSERT_EQUAL_UINT8(0, atomicSlotNewest(true, 5, true, 5));
  TEST_ASSERT_EQUAL_UINT8(0, atomicSlotNewest(true, 5, true, 4));
}
void test_generation_wraparound() {
  TEST_ASSERT_EQUAL_UINT8(1, atomicSlotNewest(true, 0xFFFFFFFEUL, true, 1));
  TEST_ASSERT_EQUAL_UINT8(0, atomicSlotNewest(true, 1, true, 0xFFFFFFFEUL));
}
void test_falls_back_when_newest_second_read_fails() {
  uint8_t attempts[2] = {0, 0};
  bool ok = atomicSlotLoadWithFallback(true, true, 1, [&](uint8_t slot) {
    attempts[slot]++;
    return slot == 0;
  });
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8(1, attempts[1]);
  TEST_ASSERT_EQUAL_UINT8(1, attempts[0]);
}
void test_does_not_attempt_invalid_fallback() {
  uint8_t attempts = 0;
  bool ok = atomicSlotLoadWithFallback(true, false, 0, [&](uint8_t) {
    attempts++;
    return false;
  });
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT8(1, attempts);
}
void test_both_slots_invalid() {
  uint8_t attempts = 0;
  bool ok = atomicSlotLoadWithFallback(false, false, 0, [&](uint8_t) {
    attempts++;
    return true;
  });
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT8(0, attempts);
}
int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_selects_only_valid_slot);
  RUN_TEST(test_newest_generation_and_equal_tie);
  RUN_TEST(test_generation_wraparound);
  RUN_TEST(test_falls_back_when_newest_second_read_fails);
  RUN_TEST(test_does_not_attempt_invalid_fallback);
  RUN_TEST(test_both_slots_invalid);
  return UNITY_END();
}