#include <bike/protocol.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(Protocol, EncodeHeaderLayout) {
    // magic(4) + eid u16 LE(2) + seq u32 LE(4) + len i32 LE(4) = 14
    Frame f{.event_id = 0x0102, .seq = 0x0A0B0C0D, .payload = std::string(4, '\0')};
    auto bytes = encode(f);
    ASSERT_EQ(bytes.size(), kHeaderLen + 4u);
    EXPECT_EQ(bytes[0], 'F');
    EXPECT_EQ(bytes[1], 'B');
    EXPECT_EQ(bytes[2], 'E');
    EXPECT_EQ(bytes[3], 'B');
    // event_id u16 LE @ [4,6)
    EXPECT_EQ(bytes[4], 0x02);
    EXPECT_EQ(bytes[5], 0x01);
    // seq u32 LE @ [6,10)
    EXPECT_EQ(bytes[6], 0x0D);
    EXPECT_EQ(bytes[7], 0x0C);
    EXPECT_EQ(bytes[8], 0x0B);
    EXPECT_EQ(bytes[9], 0x0A);
    // len i32 LE @ [10,14)
    EXPECT_EQ(bytes[10], 4);
    EXPECT_EQ(bytes[11], 0);
    EXPECT_EQ(bytes[12], 0);
    EXPECT_EQ(bytes[13], 0);
}

TEST(Protocol, HandlerStyleDesignatedInitStillCompiles) {
    // bike_server_core 的 handler 全部使用 {.event_id=.., .payload=..} 形式,
    // 新增 seq 字段后必须保持可编译且 seq 默认 0。
    Frame f{.event_id = 0x04, .payload = "x"};
    EXPECT_EQ(f.seq, 0u);
    auto bytes = encode(f);
    auto r = decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->frame.seq, 0u);
}

TEST(Protocol, DecodeRoundTripWithSeq) {
    Frame original{.event_id = 0x0301, .seq = 777, .payload = "hello world"};
    auto bytes = encode(original);
    auto r = decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->frame.event_id, original.event_id);
    EXPECT_EQ(r->frame.seq, original.seq);
    EXPECT_EQ(r->frame.payload, original.payload);
    EXPECT_EQ(r->consumed, bytes.size());
}

TEST(Protocol, DecodeRejectsBadMagic) {
    std::vector<std::uint8_t> bad(kHeaderLen, 0);
    bad[0] = 'X'; bad[1] = 'X'; bad[2] = 'X'; bad[3] = 'X';
    EXPECT_FALSE(decode(bad.data(), bad.size()).has_value());
}

TEST(Protocol, DecodeRejectsOversize) {
    std::vector<std::uint8_t> bad(kHeaderLen, 0);
    bad[0] = 'F'; bad[1] = 'B'; bad[2] = 'E'; bad[3] = 'B';
    bad[4] = 1; bad[5] = 0;
    auto oversize = kMaxMessageLen + 1;
    bad[10] = oversize & 0xFF;
    bad[11] = (oversize >> 8) & 0xFF;
    bad[12] = (oversize >> 16) & 0xFF;
    bad[13] = (oversize >> 24) & 0xFF;
    EXPECT_FALSE(decode(bad.data(), bad.size()).has_value());
}

TEST(Protocol, DecodeRejectsShortBuffer) {
    Frame f{.event_id = 1, .payload = std::string(20, 'a')};
    auto bytes = encode(f);
    // 不足帧头 / 不足帧体 均为半包
    EXPECT_FALSE(decode(bytes.data(), kHeaderLen - 1).has_value());
    EXPECT_FALSE(decode(bytes.data(), bytes.size() - 1).has_value());
}

TEST(Protocol, EncodeRejectsHugePayload) {
    Frame f{.event_id = 1, .payload = std::string(kMaxMessageLen + 1, 'x')};
    EXPECT_THROW(encode(f), std::overflow_error);
}

TEST(Protocol, DecodeFrameNeedMoreOnPartialHeader) {
    Frame f{.event_id = 0x11, .seq = 5, .payload = "abc"};
    auto bytes = encode(f);
    auto r = decode_frame(bytes.data(), kHeaderLen - 1);
    EXPECT_EQ(r.status, DecodeStatus::NeedMore);
}

TEST(Protocol, DecodeFrameNeedMoreOnPartialBody) {
    Frame f{.event_id = 0x11, .seq = 5, .payload = "abcdef"};
    auto bytes = encode(f);
    auto r = decode_frame(bytes.data(), bytes.size() - 1);
    EXPECT_EQ(r.status, DecodeStatus::NeedMore);
}

TEST(Protocol, DecodeFrameBadFrameOnBadMagic) {
    Frame f{.event_id = 0x11, .seq = 5, .payload = "abc"};
    auto bytes = encode(f);
    bytes[0] = 'Z';
    auto r = decode_frame(bytes.data(), bytes.size());
    EXPECT_EQ(r.status, DecodeStatus::BadFrame);
}

TEST(Protocol, DecodeFrameBadFrameOnNegativeLen) {
    std::vector<std::uint8_t> bad(kHeaderLen, 0);
    bad[0] = 'F'; bad[1] = 'B'; bad[2] = 'E'; bad[3] = 'B';
    bad[10] = 0xFF; bad[11] = 0xFF; bad[12] = 0xFF; bad[13] = 0xFF; // len = -1
    auto r = decode_frame(bad.data(), bad.size());
    EXPECT_EQ(r.status, DecodeStatus::BadFrame);
}

TEST(Protocol, DecodeFrameOkReturnsSeq) {
    Frame f{.event_id = 0x13, .seq = 0xDEADBEEF, .payload = "seq-test"};
    auto bytes = encode(f);
    auto r = decode_frame(bytes.data(), bytes.size());
    ASSERT_EQ(r.status, DecodeStatus::Ok);
    EXPECT_EQ(r.frame.seq, 0xDEADBEEFu);
    EXPECT_EQ(r.consumed, bytes.size());
}

TEST(Protocol, StampSeqRoundTrip) {
    Frame f{.event_id = 0x04, .payload = "resp"};  // handler 风格, seq=0
    auto bytes = encode(f);
    ASSERT_TRUE(stamp_seq(bytes, 0x01020304));
    auto r = decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->frame.seq, 0x01020304u);
    EXPECT_EQ(r->frame.payload, "resp");
}

TEST(Protocol, StampSeqRejectsShortOrBadMagic) {
    std::vector<std::uint8_t> too_short(5, 0);
    EXPECT_FALSE(stamp_seq(too_short, 1));

    std::vector<std::uint8_t> bad_magic(kHeaderLen, 0);
    EXPECT_FALSE(stamp_seq(bad_magic, 1));
}
