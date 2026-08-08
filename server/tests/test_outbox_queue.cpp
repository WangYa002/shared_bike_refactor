// OutboxQueue 纯逻辑测试: 单线程语义 + 多线程 MPSC 压力 (跨平台)。

#include "server/gateway/outbox_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

using namespace bike::gateway;

TEST(OutboxQueue, PushDrainOrder) {
    OutboxQueue q;
    q.push(OutboxItem{OutboxKind::Respond, 1, {0xAA}});
    q.push(OutboxItem{OutboxKind::RearmRecv, 2, {0xBB, 0xCC}});
    q.push(OutboxItem{OutboxKind::Close, 3, {}});

    std::vector<OutboxItem> out;
    EXPECT_EQ(q.drain(out), 3u);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].kind, OutboxKind::Respond);
    EXPECT_EQ(out[0].conn_id, 1u);
    ASSERT_EQ(out[0].bytes.size(), 1u);
    EXPECT_EQ(out[0].bytes[0], 0xAA);
    EXPECT_EQ(out[1].kind, OutboxKind::RearmRecv);
    EXPECT_EQ(out[1].bytes.size(), 2u);
    EXPECT_EQ(out[2].kind, OutboxKind::Close);
    EXPECT_TRUE(out[2].bytes.empty());
}

TEST(OutboxQueue, DrainEmptyReturnsZero) {
    OutboxQueue q;
    std::vector<OutboxItem> out;
    EXPECT_EQ(q.drain(out), 0u);
    EXPECT_TRUE(out.empty());
}

TEST(OutboxQueue, DrainAccumulatesAcrossCalls) {
    OutboxQueue q;
    std::vector<OutboxItem> out;
    q.push(OutboxItem{OutboxKind::Close, 1, {}});
    q.drain(out);
    q.push(OutboxItem{OutboxKind::Close, 2, {}});
    EXPECT_EQ(q.drain(out), 1u);
    EXPECT_EQ(out.size(), 2u);   // drain 追加而非覆盖
}

TEST(OutboxQueue, MultiProducerStress) {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 2000;

    OutboxQueue q;
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int t = 0; t < kProducers; ++t) {
        producers.emplace_back([&q, t] {
            for (int i = 0; i < kPerProducer; ++i) {
                q.push(OutboxItem{OutboxKind::Respond,
                                  static_cast<std::uint64_t>(t),
                                  {static_cast<std::uint8_t>(i & 0xFF)}});
            }
        });
    }

    // 消费者边生产边 drain, 最终总数必须一致
    std::size_t total = 0;
    std::vector<std::uint64_t> per_producer(kProducers, 0);
    std::vector<OutboxItem> out;
    while (total < static_cast<std::size_t>(kProducers) * kPerProducer) {
        out.clear();
        total += q.drain(out);
        for (auto& it : out) {
            ASSERT_LT(it.conn_id, static_cast<std::uint64_t>(kProducers));
            per_producer[it.conn_id]++;
        }
    }
    for (auto& th : producers) th.join();

    EXPECT_EQ(total, static_cast<std::size_t>(kProducers) * kPerProducer);
    for (int t = 0; t < kProducers; ++t)
        EXPECT_EQ(per_producer[static_cast<std::size_t>(t)],
                  static_cast<std::uint64_t>(kPerProducer));
}
