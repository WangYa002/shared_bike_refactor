// 模块三 SpscRing 纯逻辑测试 (跨平台, 设计稿 §10.1)。
// 环不依赖 mmap: attach 指针指向堆内存即可完整验证无锁协议,
// 含满/空边界、批量两阶段、回绕点截断、32 位差值语义与 SPSC 多线程压力。

#include "bike/ipc/spsc_ring.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

using namespace bike::ipc;

namespace {

// 测试用小环: 128B 槽 × 8 个
using TinyRing = SpscRing<128, 3>;
// 压力测试环: 64B 槽 × 1024 个
using StressRing = SpscRing<64, 10>;

// 堆内存承载环结构(模拟 shm attach): RingHeader + Slot[]。
template <class Ring>
struct HeapRing {
    RingHeader header;                    // NSDMI 归零
    std::vector<typename Ring::Slot> slots{Ring::kSlotCount};

    typename Ring::Producer producer() { return typename Ring::Producer(&header, slots.data()); }
    typename Ring::Consumer consumer() { return typename Ring::Consumer(&header, slots.data()); }
};

IpcPacket make_pkt(std::uint64_t conn, std::uint32_t seq, std::uint32_t len) {
    IpcPacket p;
    p.conn_id = conn;
    p.event_id = 0x0001;
    p.seq = seq;
    p.payload_len = len;
    return p;
}

} // namespace

TEST(SpscRing, EmptyAndFullBoundaries) {
    HeapRing<TinyRing> heap;
    auto prod = heap.producer();
    auto cons = heap.consumer();

    EXPECT_TRUE(prod.empty());
    EXPECT_TRUE(cons.empty());
    EXPECT_FALSE(prod.full());

    const std::uint8_t pay[4] = {1, 2, 3, 4};
    for (std::uint32_t i = 0; i < TinyRing::kSlotCount; ++i) {
        EXPECT_TRUE(prod.push(make_pkt(i, i, 4), pay));
    }
    EXPECT_TRUE(prod.full());
    EXPECT_FALSE(prod.push(make_pkt(99, 99, 4), pay));   // 满: 拒绝

    // 消费全部 8 个 → 环清空
    const TinyRing::Slot* out = nullptr;
    EXPECT_EQ(cons.pop_batch(&out, 64), TinyRing::kSlotCount);
    EXPECT_EQ(out[0].pkt.conn_id, 0u);
    EXPECT_EQ(out[TinyRing::kSlotCount - 1].pkt.conn_id,
              TinyRing::kSlotCount - 1);
    cons.release(TinyRing::kSlotCount);
    EXPECT_TRUE(prod.empty());

    EXPECT_TRUE(prod.push(make_pkt(100, 100, 4), pay));
    EXPECT_FALSE(prod.empty());
    // 再填满 7 个 → 再次满, 验证满→空→满循环
    for (std::uint32_t i = 101; i < 100 + TinyRing::kSlotCount; ++i)
        EXPECT_TRUE(prod.push(make_pkt(i, i, 4), pay));
    EXPECT_TRUE(prod.full());
    EXPECT_FALSE(prod.push(make_pkt(999, 999, 4), pay));
}

TEST(SpscRing, PushPopDataIntegrity) {
    HeapRing<TinyRing> heap;
    auto prod = heap.producer();
    auto cons = heap.consumer();

    std::uint8_t pay[TinyRing::kPayloadMax];
    for (std::size_t i = 0; i < sizeof(pay); ++i)
        pay[i] = static_cast<std::uint8_t>(i * 7 + 3);

    ASSERT_TRUE(prod.push(make_pkt(0xABC, 77, sizeof(pay)), pay));

    const TinyRing::Slot* out = nullptr;
    ASSERT_EQ(cons.pop_batch(&out, 8), 1u);
    EXPECT_EQ(out[0].pkt.conn_id, 0xABCu);
    EXPECT_EQ(out[0].pkt.seq, 77u);
    EXPECT_EQ(out[0].pkt.payload_len, sizeof(pay));
    EXPECT_EQ(std::memcmp(out[0].payload, pay, sizeof(pay)), 0);
    cons.release(1);
    EXPECT_TRUE(cons.empty());
}

TEST(SpscRing, BatchTwoPhaseWrite) {
    HeapRing<TinyRing> heap;
    auto prod = heap.producer();
    auto cons = heap.consumer();

    // 连续 begin_write 4 次(未 publish 前 consumer 不可见)
    constexpr std::uint32_t kBatch = 4;
    for (std::uint32_t i = 0; i < kBatch; ++i) {
        auto* slot = prod.begin_write();
        ASSERT_NE(slot, nullptr);
        const std::uint8_t b = static_cast<std::uint8_t>(i);
        TinyRing::Producer::fill(slot, make_pkt(i, i, 1), &b);
    }
    EXPECT_TRUE(cons.empty());          // 未发布: consumer 看不到
    prod.publish(kBatch);               // 一次性 release 发布

    const TinyRing::Slot* out = nullptr;
    ASSERT_EQ(cons.pop_batch(&out, 64), kBatch);
    for (std::uint32_t i = 0; i < kBatch; ++i) {
        EXPECT_EQ(out[i].pkt.conn_id, i);
        EXPECT_EQ(out[i].payload[0], static_cast<std::uint8_t>(i));
    }
    cons.release(kBatch);
}

TEST(SpscRing, PopBatchTruncatesAtWrapPoint) {
    HeapRing<TinyRing> heap;
    auto prod = heap.producer();
    auto cons = heap.consumer();
    const std::uint8_t pay = 0xEE;

    // 先推 6 弹 6, 让 tail 停在逻辑位置 6(物理下标 6)
    for (int i = 0; i < 6; ++i) ASSERT_TRUE(prod.push(make_pkt(i, i, 1), &pay));
    const TinyRing::Slot* out = nullptr;
    ASSERT_EQ(cons.pop_batch(&out, 64), 6u);
    cons.release(6);

    // 再推 4: 物理槽 6,7,0,1 —— 跨回绕点
    for (int i = 6; i < 10; ++i) ASSERT_TRUE(prod.push(make_pkt(i, i, 1), &pay));
    // 首批只能取到数组尾部: 2 个(物理 6,7)
    ASSERT_EQ(cons.pop_batch(&out, 8), 2u);
    EXPECT_EQ(out[0].pkt.conn_id, 6u);
    EXPECT_EQ(out[1].pkt.conn_id, 7u);
    cons.release(2);
    // 次批取回绕后的 2 个(物理 0,1)
    ASSERT_EQ(cons.pop_batch(&out, 8), 2u);
    EXPECT_EQ(out[0].pkt.conn_id, 8u);
    EXPECT_EQ(out[1].pkt.conn_id, 9u);
    cons.release(2);
    EXPECT_TRUE(cons.empty());
}

TEST(SpscRing, WraparoundLongRun) {
    // 8 槽小环连续写读 10^5 次: 覆盖 head/tail 反复回绕数组边界
    HeapRing<TinyRing> heap;
    auto prod = heap.producer();
    auto cons = heap.consumer();

    for (std::uint64_t i = 0; i < 100000; ++i) {
        const std::uint8_t pay = static_cast<std::uint8_t>(i & 0xFF);
        ASSERT_TRUE(prod.push(make_pkt(i, static_cast<std::uint32_t>(i), 1), &pay));
        const TinyRing::Slot* out = nullptr;
        ASSERT_EQ(cons.pop_batch(&out, 4), 1u);
        ASSERT_EQ(out[0].pkt.conn_id, i);
        ASSERT_EQ(out[0].payload[0], pay);
        cons.release(1);
    }
    EXPECT_TRUE(prod.empty());
}

TEST(SpscRing, MultiThreadedStress) {
    // 2 线程 × 200k 条: release/acquire 配对下的正确性(顺序 + 校验和)
    HeapRing<StressRing> heap;
    constexpr std::uint32_t kTotal = 200000;
    std::atomic<bool> done{false};
    std::uint64_t consumed_sum = 0;
    std::uint32_t consumed_cnt = 0;

    std::thread producer([&] {
        auto prod = heap.producer();
        for (std::uint32_t i = 0; i < kTotal; ++i) {
            std::uint8_t pay[8];
            std::memcpy(pay, &i, sizeof(i));
            // 环满则重试(SPSC 压力下 producer 允许自旋)
            while (!prod.push(make_pkt(i, i, sizeof(pay)), pay)) {
#if defined(_MSC_VER)
                std::this_thread::yield();
#else
                __builtin_ia32_pause();
#endif
            }
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        auto cons = heap.consumer();
        std::uint32_t expect_next = 0;
        while (consumed_cnt < kTotal) {
            const StressRing::Slot* out = nullptr;
            std::uint32_t n = cons.pop_batch(&out, 256);
            if (n == 0) {
                if (done.load(std::memory_order_acquire) && cons.empty()) break;
                std::this_thread::yield();
                continue;
            }
            for (std::uint32_t i = 0; i < n; ++i) {
                ASSERT_EQ(out[i].pkt.conn_id, expect_next);   // 严格保序
                std::uint32_t v;
                std::memcpy(&v, out[i].payload, sizeof(v));
                ASSERT_EQ(v, expect_next);
                consumed_sum += v;
                ++expect_next;
            }
            consumed_cnt += n;
            cons.release(n);
        }
        EXPECT_EQ(expect_next, kTotal);
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(consumed_cnt, kTotal);
    // 0+1+...+(kTotal-1)
    EXPECT_EQ(consumed_sum,
              static_cast<std::uint64_t>(kTotal - 1) * kTotal / 2);
}
