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
