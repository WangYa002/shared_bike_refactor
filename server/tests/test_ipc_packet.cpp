// 模块三 ipc_packet/shm_layout 布局测试 (跨平台, 设计稿 §10.1)。

#include "bike/ipc/ipc_packet.hpp"
#include "bike/ipc/shm_layout.hpp"
#include "bike/ipc/spsc_ring.hpp"
#include "bike/ipc/ipc_errors.hpp"
#include "bike/ipc/reply_queue.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace bike::ipc;

TEST(IpcPacket, LayoutConstants) {
    EXPECT_EQ(sizeof(IpcPacket), 32u);
    EXPECT_EQ(sizeof(ShmFileHeader), 128u);
    EXPECT_EQ(sizeof(RingHeader), 128u);
    EXPECT_EQ(sizeof(RingSlotT<kReqSlotSize>), kReqSlotSize);
    EXPECT_EQ(sizeof(RingSlotT<kRspSlotSize>), kRspSlotSize);

    // 响应槽必须装得下协议最大 payload(372680)
    EXPECT_GE(kRspPayloadMax, 372680u);
    // head/tail 分属不同缓存行
    EXPECT_GE(offsetof(RingHeader, tail) - offsetof(RingHeader, head), kCacheLine);
}

TEST(IpcPacket, FieldRoundtripViaMemcpy) {
    // 位置无关性验证: 整体 memcpy 到另一块内存后字段语义不变
    IpcPacket src;
    src.conn_id = 0xDEADBEEF12345678ull;
    src.event_id = 0x0016;
    src.flags = kFlagOneWay;
    src.seq = 42;
    src.payload_len = 7;
    // reserved0/reserved1 由 NSDMI 归零

    alignas(8) std::uint8_t buf[sizeof(IpcPacket)];
    std::memcpy(buf, &src, sizeof(src));
    IpcPacket dst;
    std::memcpy(&dst, buf, sizeof(dst));

    EXPECT_EQ(dst.conn_id, src.conn_id);
    EXPECT_EQ(dst.event_id, src.event_id);
    EXPECT_EQ(dst.flags, src.flags);
    EXPECT_EQ(dst.seq, src.seq);
    EXPECT_EQ(dst.payload_len, src.payload_len);
}

TEST(IpcPacket, RingInstantiations) {
    EXPECT_EQ(ReqRing::kSlotCount, 512u);
    EXPECT_EQ(ReqRing::kPayloadMax, kReqPayloadMax);
    EXPECT_EQ(RspRing::kSlotCount, 256u);
    EXPECT_EQ(RspRing::kPayloadMax, kRspPayloadMax);
}

TEST(IpcPacket, FileLayoutComputation) {
    // 16 个请求环: 头区 128 + 16*128 = 2176 → 4K 对齐到 4096
    RingFileLayout l = compute_file_layout(16, ReqRing::kRegionBytes);
    EXPECT_EQ(l.headers_offset, sizeof(ShmFileHeader));
    EXPECT_EQ(l.slots_offset % kPageAlign, 0u);
    EXPECT_GE(l.slots_offset,
              sizeof(ShmFileHeader) + 16 * sizeof(RingHeader));
    EXPECT_EQ(l.ring_stride, ReqRing::kRegionBytes);
    EXPECT_EQ(l.total_bytes, l.slots_offset + 16 * ReqRing::kRegionBytes);

    // 响应环文件(1 环)
    RingFileLayout r = compute_file_layout(1, RspRing::kRegionBytes);
    EXPECT_EQ(r.total_bytes, r.slots_offset + RspRing::kRegionBytes);
}

TEST(IpcErrors, TypesAreCatchableAsStdException) {
    try {
        throw SinkOverload();
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("ring full"), std::string::npos);
    }
    try {
        throw MalformedIpcRequest();
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("kReqPayloadMax"), std::string::npos);
    }
}

TEST(ReplyQueue, MpscPushDrain) {
    ReplyQueue q;
    for (int i = 0; i < 5; ++i) {
        ReplyItem it;
        it.conn_id = static_cast<std::uint64_t>(i);
        it.seq = static_cast<std::uint32_t>(i * 10);
        q.push(std::move(it));
    }
    EXPECT_EQ(q.size(), 5u);
    std::vector<ReplyItem> out;
    EXPECT_EQ(q.drain(out), 5u);
    EXPECT_EQ(q.drain(out), 0u);   // 已取空: 二次 drain 为 0
    EXPECT_EQ(out.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(out[i].conn_id, static_cast<std::uint64_t>(i));
        EXPECT_EQ(out[i].seq, static_cast<std::uint32_t>(i * 10));
    }
    EXPECT_EQ(q.size(), 0u);
}
