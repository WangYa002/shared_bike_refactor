// 切帧纯逻辑测试: 粘包/半包/坏帧/leftover 闭环 (跨平台, 无系统调用)。

#include "server/gateway/frame_cut.hpp"

#include <bike/protocol.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using namespace bike::gateway;

namespace {
std::vector<std::uint8_t> mk(std::uint16_t eid, std::uint32_t seq,
                             const std::string& payload) {
    return bike::encode(bike::Frame{.event_id = eid, .seq = seq, .payload = payload});
}
} // namespace

TEST(FrameCut, EmptyInput) {
    std::vector<bike::Frame> frames;
    auto r = cut_frames(nullptr, 0, frames);
    EXPECT_FALSE(r.bad);
    EXPECT_TRUE(frames.empty());
    EXPECT_EQ(r.leftover_len, 0u);
}

TEST(FrameCut, MultipleFramesInOneChunk) {
    // 粘包: 一次 recv 携带 3 帧, 全部切出
    std::vector<std::uint8_t> buf;
    auto a = mk(0x01, 1, "aaa");
    auto b = mk(0x03, 2, "bb");
    auto c = mk(0x15, 3, "");
    buf.insert(buf.end(), a.begin(), a.end());
    buf.insert(buf.end(), b.begin(), b.end());
    buf.insert(buf.end(), c.begin(), c.end());

    std::vector<bike::Frame> frames;
    auto r = cut_frames(buf.data(), buf.size(), frames);
    EXPECT_FALSE(r.bad);
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(frames[0].event_id, 0x01);
    EXPECT_EQ(frames[0].seq, 1u);
    EXPECT_EQ(frames[0].payload, "aaa");
    EXPECT_EQ(frames[1].event_id, 0x03);
    EXPECT_EQ(frames[1].seq, 2u);
    EXPECT_EQ(frames[1].payload, "bb");
    EXPECT_EQ(frames[2].event_id, 0x15);
    EXPECT_TRUE(frames[2].payload.empty());
    EXPECT_EQ(r.leftover_len, 0u);
}

TEST(FrameCut, PartialFrameLeftover) {
    // 半包: 完整帧 + 半个帧头, leftover 指向半个帧头
    auto full = mk(0x01, 9, "x");
    std::vector<std::uint8_t> buf = full;
    buf.insert(buf.end(), {'F', 'B', 'E'});   // 半个 magic

    std::vector<bike::Frame> frames;
    auto r = cut_frames(buf.data(), buf.size(), frames);
    EXPECT_FALSE(r.bad);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].seq, 9u);
    ASSERT_EQ(r.leftover_len, 3u);
    EXPECT_EQ(std::memcmp(r.leftover, "FBE", 3), 0);
}

TEST(FrameCut, PartialBodyLeftover) {
    // 半包: 帧头完整但 payload 未到齐
    auto full = mk(0x05, 7, "0123456789");
    std::vector<std::uint8_t> buf(full.begin(), full.begin() + bike::kHeaderLen + 4);

    std::vector<bike::Frame> frames;
    auto r = cut_frames(buf.data(), buf.size(), frames);
    EXPECT_FALSE(r.bad);
    EXPECT_TRUE(frames.empty());
    EXPECT_EQ(r.leftover_len, buf.size());
    EXPECT_EQ(r.leftover, buf.data());
}

TEST(FrameCut, ByteByByteFeedingSimulatesRearmLoop) {
    // 模拟主循环: 逐字节喂入, 每轮切帧, leftover 与下一轮拼接
    auto f1 = mk(0x01, 100, "hello");
    auto f2 = mk(0x04, 101, "world!");

    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());

    std::vector<std::uint8_t> pending;   // 模拟 rx 累积区
    std::vector<bike::Frame> got;
    for (std::uint8_t byte : stream) {
        pending.push_back(byte);
        std::vector<bike::Frame> frames;
        auto r = cut_frames(pending.data(), pending.size(), frames);
        ASSERT_FALSE(r.bad);
        got.insert(got.end(), std::make_move_iterator(frames.begin()),
                   std::make_move_iterator(frames.end()));
        pending.assign(r.leftover, r.leftover + r.leftover_len);  // leftover 回填
    }
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].seq, 100u);
    EXPECT_EQ(got[0].payload, "hello");
    EXPECT_EQ(got[1].seq, 101u);
    EXPECT_EQ(got[1].payload, "world!");
    EXPECT_TRUE(pending.empty());
}

TEST(FrameCut, BadMagicFlagsBad) {
    std::vector<std::uint8_t> buf = {'X', 'Y', 'Z', '!', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<bike::Frame> frames;
    auto r = cut_frames(buf.data(), buf.size(), frames);
    EXPECT_TRUE(r.bad);
    EXPECT_TRUE(frames.empty());
}

TEST(FrameCut, BadMagicAfterGoodFrameStillFlagsBad) {
    auto good = mk(0x01, 1, "ok");
    std::vector<std::uint8_t> buf = good;
    std::vector<std::uint8_t> garbage(16, 0xAB);
    buf.insert(buf.end(), garbage.begin(), garbage.end());

    std::vector<bike::Frame> frames;
    auto r = cut_frames(buf.data(), buf.size(), frames);
    EXPECT_TRUE(r.bad);
    EXPECT_EQ(frames.size(), 1u);   // 坏帧前的完整帧仍被切出
}

TEST(FrameCut, OversizeLenFlagsBad) {
    std::vector<std::uint8_t> buf(bike::kHeaderLen, 0);
    buf[0] = 'F'; buf[1] = 'B'; buf[2] = 'E'; buf[3] = 'B';
    auto oversize = bike::kMaxMessageLen + 1;
    buf[10] = oversize & 0xFF;
    buf[11] = (oversize >> 8) & 0xFF;
    buf[12] = (oversize >> 16) & 0xFF;
    buf[13] = (oversize >> 24) & 0xFF;
    std::vector<bike::Frame> frames;
    auto r = cut_frames(buf.data(), buf.size(), frames);
    EXPECT_TRUE(r.bad);
}
