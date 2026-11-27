#include <gtest/gtest.h>
#include <stdint.h>
#include "helpers/RoomSequence.h"

TEST(RoomSequence, OrdinaryOrdering) {
  EXPECT_TRUE(roomSequenceIsNewer(12, 11));
  EXPECT_FALSE(roomSequenceIsNewer(11, 12));
  EXPECT_FALSE(roomSequenceIsNewer(12, 12));
}

TEST(RoomSequence, HandlesUint32Rollover) {
  EXPECT_TRUE(roomSequenceIsNewer(0, 0xFFFFFFFFUL));
  EXPECT_TRUE(roomSequenceIsNewer(1, 0xFFFFFFFFUL));
  EXPECT_FALSE(roomSequenceIsNewer(0xFFFFFFFFUL, 0));
}

TEST(RoomSequence, HandlesValuesNearRollover) {
  EXPECT_TRUE(roomSequenceIsNewer(0xFFFFFFFFUL, 0xFFFFFFFEUL));
  EXPECT_TRUE(roomSequenceIsNewer(2, 0xFFFFFFFFUL));
  EXPECT_FALSE(roomSequenceIsNewer(0xFFFFFFFEUL, 1));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
