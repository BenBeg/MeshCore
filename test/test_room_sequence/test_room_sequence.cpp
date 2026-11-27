#include <gtest/gtest.h>
#include <stdint.h>
#include "helpers/RoomSequence.h"

TEST(RoomSequence, TracksOrdinaryOrdering) {
  RoomSequenceTracker tracker;
  tracker.observe(10);
  tracker.observe(12);
  tracker.observe(11);
  EXPECT_TRUE(tracker.hasValue());
  EXPECT_EQ(12U, tracker.value());
}

TEST(RoomSequence, InitializesFromHighFirstSequence) {
  RoomSequenceTracker tracker;
  tracker.observe(0xFFFFFFFEUL);
  EXPECT_EQ(0xFFFFFFFEUL, tracker.value());
  tracker.observe(0xFFFFFFFFUL);
  EXPECT_EQ(0xFFFFFFFFUL, tracker.value());
}

TEST(RoomSequence, HandlesUint32Rollover) {
  RoomSequenceTracker tracker;
  tracker.observe(0xFFFFFFFEUL);
  tracker.observe(0xFFFFFFFFUL);
  tracker.observe(0);
  tracker.observe(1);
  EXPECT_EQ(1U, tracker.value());
}

TEST(RoomSequence, RejectsStalePreRolloverSequence) {
  RoomSequenceTracker tracker;
  tracker.observe(0);
  tracker.observe(0xFFFFFFFFUL);
  EXPECT_EQ(0U, tracker.value());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
