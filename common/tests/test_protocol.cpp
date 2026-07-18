#include <bike/protocol.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(Protocol, EncodeHeaderLayout) {
    Frame f{.event_id = 0x0102, .payload = std::string(4, '\0')};
    auto bytes = encode(f);
    ASSERT_EQ(bytes.size(), kHeaderLen + 4u);
    EXPECT_EQ(bytes[0], 'F');
    EXPECT_EQ(bytes[1], 'B');
    EXPECT_EQ(bytes[2], 'E');
    EXPECT_EQ(bytes[3], 'B');
    EXPECT_EQ(bytes[4], 0x02);
    EXPECT_EQ(bytes[5], 0x01);
    EXPECT_EQ(bytes[6], 4);
    EXPECT_EQ(bytes[7], 0);
    EXPECT_EQ(bytes[8], 0);
    EXPECT_EQ(bytes[9], 0);
}

TEST(Protocol, DecodeRoundTrip) {
    Frame original{.event_id = 0x0301, .payload = "hello world"};
    auto bytes = encode(original);
    auto r = decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->frame.event_id, original.event_id);
    EXPECT_EQ(r->frame.payload, original.payload);
    EXPECT_EQ(r->consumed, bytes.size());
}

TEST(Protocol, DecodeRejectsBadMagic) {
    std::vector<std::uint8_t> bad = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0};
    EXPECT_FALSE(decode(bad.data(), bad.size()).has_value());
}

TEST(Protocol, DecodeRejectsOversize) {
    std::vector<std::uint8_t> bad(10);
    bad[0] = 'F'; bad[1] = 'B'; bad[2] = 'E'; bad[3] = 'B';
    bad[4] = 1; bad[5] = 0;
    auto oversize = kMaxMessageLen + 1;
    bad[6] = oversize & 0xFF;
    bad[7] = (oversize >> 8) & 0xFF;
    bad[8] = (oversize >> 16) & 0xFF;
    bad[9] = (oversize >> 24) & 0xFF;
    EXPECT_FALSE(decode(bad.data(), bad.size()).has_value());
}

TEST(Protocol, DecodeRejectsShortBuffer) {
    Frame f{.event_id = 1, .payload = std::string(20, 'a')};
    auto bytes = encode(f);
    EXPECT_FALSE(decode(bytes.data(), 12).has_value());
}

TEST(Protocol, EncodeRejectsHugePayload) {
    Frame f{.event_id = 1, .payload = std::string(kMaxMessageLen + 1, 'x')};
    EXPECT_THROW(encode(f), std::overflow_error);
}
