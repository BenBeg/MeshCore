#include <gtest/gtest.h>
#include "Packet.h"

using namespace mesh;

TEST(PacketPathLength, AcceptsValidEncodedSizesAtBoundary) {
    EXPECT_TRUE(Packet::isValidPathLen(0x3F)); // 63 one-byte hashes
    EXPECT_TRUE(Packet::isValidPathLen(0x60)); // 32 two-byte hashes = 64 bytes
    EXPECT_TRUE(Packet::isValidPathLen(0x95)); // 21 three-byte hashes = 63 bytes
}

TEST(PacketPathLength, RejectsEncodedLengthsOverBufferCapacity) {
    EXPECT_FALSE(Packet::isValidPathLen(0x61)); // 33 two-byte hashes = 66 bytes
    EXPECT_FALSE(Packet::isValidPathLen(0x96)); // 22 three-byte hashes = 66 bytes
}

TEST(PacketPathLength, RejectsReservedFourByteHashEncoding) {
    EXPECT_FALSE(Packet::isValidPathLen(0xC0));
    EXPECT_FALSE(Packet::isValidPathLen(0xFF));
}
