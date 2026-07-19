#include <bike/geo.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(Geo, HaversineZero) {
    EXPECT_NEAR(haversine_m(39.98, 116.31, 39.98, 116.31), 0.0, 0.01);
}

TEST(Geo, HaversineKnownPair) {
    // 北京天安门到天津火车站,大圆距离 ~114 km (公路距离 ~117 km,大圆更短)
    double d = haversine_m(39.9087, 116.3974, 39.1308, 117.2607);
    EXPECT_NEAR(d, 113865.0, 500.0);
}

TEST(Geo, IsInChinaBeijing) {
    EXPECT_TRUE(is_in_china(39.98, 116.31));
}

TEST(Geo, IsInChinaRejectsNewYork) {
    EXPECT_FALSE(is_in_china(40.71, -74.00));
}

TEST(Geo, IsInChinaRejectsNullIsland) {
    EXPECT_FALSE(is_in_china(0.0, 0.0));
}
