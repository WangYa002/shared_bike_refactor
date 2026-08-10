#include "server/ride_session_store.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace bike::server;

TEST(RideSessionStore, CreateFindRemove) {
    RideSessionStore s;
    RideSession r{.ride_no = "R1", .user_id = 1};
    s.create(r);
    auto found = s.find("R1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->user_id, 1);
    EXPECT_TRUE(s.remove("R1"));
    EXPECT_FALSE(s.find("R1").has_value());
}

TEST(RideSessionStore, UpdatePos) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    EXPECT_TRUE(s.update_pos("R1", 39.98, 116.31, 5));
    auto found = s.find("R1");
    EXPECT_DOUBLE_EQ(found->last_lat, 39.98);
    EXPECT_EQ(found->last_seq, 5);
    ASSERT_EQ(found->points.size(), 1u);
    EXPECT_EQ(found->points[0].seq, 5);
}

// 轨迹点按 seq 递增积累, elapsed_sec 随点存入。
TEST(RideSessionStore, PointsAccumulateInOrder) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    for (int i = 1; i <= 10; ++i)
        EXPECT_TRUE(s.update_pos("R1", 39.98 + i * 0.001, 116.31 + i * 0.001, i, i));
    auto found = s.find("R1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->points.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(found->points[i].seq, i + 1);
        EXPECT_EQ(found->points[i].elapsed_sec, i + 1);
    }
    EXPECT_EQ(found->last_seq, 10);
}

// seq 去重: 重复包与乱序包(seq <= 已见最大 seq)幂等忽略。
TEST(RideSessionStore, SeqDedupIgnoresStaleAndDuplicate) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    EXPECT_TRUE(s.update_pos("R1", 1.0, 1.0, 1, 1));
    EXPECT_TRUE(s.update_pos("R1", 3.0, 3.0, 3, 3));
    EXPECT_TRUE(s.update_pos("R1", 2.0, 2.0, 2, 2));  // 乱序 → 忽略
    EXPECT_TRUE(s.update_pos("R1", 3.5, 3.5, 3, 3));  // 重复 → 忽略
    EXPECT_TRUE(s.update_pos("R1", 4.0, 4.0, 4, 4));
    auto found = s.find("R1");
    ASSERT_EQ(found->points.size(), 3u);
    EXPECT_EQ(found->points[0].seq, 1);
    EXPECT_EQ(found->points[1].seq, 3);
    EXPECT_EQ(found->points[2].seq, 4);
    EXPECT_EQ(found->last_seq, 4);
    EXPECT_DOUBLE_EQ(found->last_lat, 4.0);
}

// 上限保护: 超过 3600 点后丢弃最旧的点。
TEST(RideSessionStore, PointsCapDropsOldest) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    const int total = 3605;
    for (int i = 1; i <= total; ++i)
        s.update_pos("R1", 39.0, 116.0, i, i);
    auto found = s.find("R1");
    ASSERT_EQ(found->points.size(), 3600u);
    // 最旧 5 个点(seq 1..5)已被丢弃, 保留 seq 6..3605。
    EXPECT_EQ(found->points.front().seq, 6);
    EXPECT_EQ(found->points.back().seq, total);
    EXPECT_EQ(found->last_seq, total);
}

// 并发写冒烟: 多线程上报同一区间(含重复/乱序), 不崩溃不丢序,
// 点序严格递增且无重复 seq, 最终 last_seq 为最大值。
TEST(RideSessionStore, ConcurrentWriteSmokePointsConsistent) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    constexpr int kThreads = 8;
    constexpr int kMaxSeq = 500;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&s] {
            for (int seq = 1; seq <= kMaxSeq; ++seq)
                s.update_pos("R1", 39.0, 116.0, seq, seq);
        });
    }
    for (auto& t : ts) t.join();
    auto found = s.find("R1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->last_seq, kMaxSeq);
    ASSERT_FALSE(found->points.empty());
    EXPECT_LE(found->points.size(), static_cast<size_t>(kMaxSeq));
    // 去重保证: seq 严格递增, 无重复无乱序。
    for (size_t i = 1; i < found->points.size(); ++i)
        EXPECT_GT(found->points[i].seq, found->points[i - 1].seq);
    // 最大值必须已入库。
    EXPECT_EQ(found->points.back().seq, kMaxSeq);
}

TEST(RideSessionStore, UpdatePosUnknownReturnsFalse) {
    RideSessionStore s;
    EXPECT_FALSE(s.update_pos("nope", 0, 0, 0));
}

TEST(RideSessionStore, ConcurrentReportsSafe) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    std::vector<std::thread> ts;
    for (int i = 0; i < 100; ++i) {
        ts.emplace_back([&s, i]{
            s.update_pos("R1", 39.98 + i*0.0001, 116.31, i);
        });
    }
    for (auto& t : ts) t.join();
    auto found = s.find("R1");
    ASSERT_TRUE(found.has_value());
    EXPECT_GE(found->last_seq, 0);
    EXPECT_LE(found->last_seq, 99);
}

TEST(RideSessionStore, UpdateAndRemoveRaceNoDeadlock) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    std::vector<std::thread> ts;
    for (int i = 0; i < 50; ++i) {
        ts.emplace_back([&s]{ s.update_pos("R1", 0, 0, 0); });
    }
    ts.emplace_back([&s]{ s.remove("R1"); });
    for (auto& t : ts) t.join();
    SUCCEED();
}
