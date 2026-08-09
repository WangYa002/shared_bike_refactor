// 切帧纯逻辑测试: 粘包/半包/坏帧/leftover 闭环 (跨平台, 无系统调用)。
// 含 Connection rx 累积区回归测试(rx_commit 覆零 bug, 部署冒烟全零帧)。

#include "server/gateway/conn_context.hpp"
#include "server/gateway/frame_cut.hpp"

#include <bike/protocol.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
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

// ============================================================================
// Connection rx 累积区回归测试 (任务 #33)
//
// 历史 bug: rx_commit 路径曾用 std::vector::resize 增长接收缓冲, resize 的
// value-init 会把内核 recv 已写入 capacity 区(data()+old_size 起)的字节覆零,
// 部署冒烟时收到全零帧。修复: 构造时一次性预分配全量存储(size==capacity),
// rx_commit 只用 rx_len_ 记账有效长度, 绝不重建元素。
// ============================================================================

// 用例一(核心回归): 模拟内核 recv 已写入数据——先向 rx 写入区写入可辨识模式,
// rx_commit 后断言字节与写入模式完全一致、未被覆零; 跨两次提交验证累积完整。
TEST(ConnRxCommit, KernelWrittenBytesSurviveCommitAcrossMultipleRecv) {
    constexpr std::size_t kCap = 64;
    Connection conn(/*fd=*/-1, /*conn_id=*/1, kCap);
    ASSERT_EQ(conn.rx_size(), 0u);

    // 第一次 recv: 取写入区并写入 8 字节 0xA5 模式(模拟内核写入 capacity 区)
    auto w1 = conn.rx_writable();
    ASSERT_EQ(w1.size(), kCap);
    std::fill_n(w1.data(), 8, std::uint8_t{0xA5});
    conn.rx_commit(8);
    EXPECT_EQ(conn.rx_size(), 8u);
    // commit 只动记账, 不得触碰存储: 刚写入的模式必须原样存活
    // (旧 resize 实现会在此处把 0xA5 覆零)
    EXPECT_EQ(std::count(w1.data(), w1.data() + 8, std::uint8_t{0xA5}), 8);

    // 第二次 recv: 写入区推进到有效长度之后, 写 12 字节递增序列
    auto w2 = conn.rx_writable();
    ASSERT_EQ(w2.size(), kCap - 8);
    ASSERT_EQ(w2.data(), w1.data() + 8);   // 累积: 紧随第一段之后
    for (std::size_t i = 0; i < 12; ++i)
        w2[i] = static_cast<std::uint8_t>(0x10 + i);
    conn.rx_commit(12);
    EXPECT_EQ(conn.rx_size(), 20u);

    // rx_take 读出累积数据: 两段模式全部完整, 无一字节被覆零
    auto data = conn.rx_take(kCap);
    ASSERT_EQ(data.size(), 20u);
    for (std::size_t i = 0; i < 8; ++i)
        EXPECT_EQ(data[i], 0xA5u) << "第一段第 " << i << " 字节被改写";
    for (std::size_t i = 0; i < 12; ++i)
        EXPECT_EQ(data[8 + i], 0x10u + i) << "第二段第 " << i << " 字节被改写";
    // 移交后累积区重置
    EXPECT_EQ(conn.rx_size(), 0u);
}

// 用例二(边界): commit(0) 不改变有效长度与写入区。
TEST(ConnRxCommit, CommitZeroIsNoOp) {
    constexpr std::size_t kCap = 16;
    Connection conn(-1, 2, kCap);

    conn.rx_commit(0);
    EXPECT_EQ(conn.rx_size(), 0u);
    EXPECT_EQ(conn.rx_writable().size(), kCap);

    // 已有有效数据后再 commit(0) 同样不变
    std::fill_n(conn.rx_writable().data(), 4, std::uint8_t{0x7E});
    conn.rx_commit(4);
    conn.rx_commit(0);
    EXPECT_EQ(conn.rx_size(), 4u);
    EXPECT_EQ(conn.rx_writable().size(), kCap - 4);
}

// 用例二(边界): commit 恰好到满容量边界——写满、提交、写区归零,
// 随后 rx_prepare 重新腾出空闲区供下一轮 recv。
TEST(ConnRxCommit, CommitToFullCapacityBoundary) {
    constexpr std::size_t kCap = 16;
    Connection conn(-1, 3, kCap);

    auto w = conn.rx_writable();
    for (std::size_t i = 0; i < kCap; ++i)
        w[i] = static_cast<std::uint8_t>(0xC0 + i);
    conn.rx_commit(kCap);
    EXPECT_EQ(conn.rx_size(), kCap);
    EXPECT_EQ(conn.rx_writable().size(), 0u);   // 满: 无空闲可写

    // rx_prepare 增长空闲区(只扩空闲尾部, 不碰已有数据)
    conn.rx_prepare(8);
    ASSERT_GE(conn.rx_writable().size(), 8u);
    conn.rx_commit(8);
    EXPECT_EQ(conn.rx_size(), kCap + 8);

    // 全量校验: 前 kCap 字节仍是原模式
    auto data = conn.rx_take(kCap);
    ASSERT_EQ(data.size(), kCap + 8);
    for (std::size_t i = 0; i < kCap; ++i)
        EXPECT_EQ(data[i], 0xC0u + i) << "满容量边界第 " << i << " 字节被改写";
}

// 用例二(边界): 超过 capacity 的 commit——代码契约由调用方保证
// n <= 空闲字节(rx_commit 无 assert/截断, 仅 rx_len_ += n 纯记账)。
// 此处断言该契约行为: 记账按算术叠加, 且存储区既有字节不受影响。
TEST(ConnRxCommit, CommitBeyondCapacityIsPureBookkeepingPerContract) {
    constexpr std::size_t kCap = 8;
    Connection conn(-1, 4, kCap);

    auto w = conn.rx_writable();
    std::fill_n(w.data(), kCap, std::uint8_t{0x5A});
    conn.rx_commit(kCap);
    conn.rx_commit(4);   // 超出 capacity: 调用方违约场景
    EXPECT_EQ(conn.rx_size(), kCap + 4);   // 纯记账, 无 clamp
    // commit 从不触碰存储: 已写入的模式仍完整存活
    EXPECT_EQ(std::count(w.data(), w.data() + kCap, std::uint8_t{0x5A}),
              static_cast<int>(kCap));
}
