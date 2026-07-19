#include <bike/validation.hpp>

#include <gtest/gtest.h>
#include <string>

using namespace bike;

TEST(Validation, BikeNoValid)         { EXPECT_TRUE(valid_bike_no("BJ-000001")); }
TEST(Validation, BikeNoRejectsEmpty)  { EXPECT_FALSE(valid_bike_no("")); }
TEST(Validation, BikeNoRejectsTooLong) {
    EXPECT_FALSE(valid_bike_no(std::string(33, 'x')));
}

TEST(Validation, LatBoundary) {
    EXPECT_FALSE(valid_lat(91.0));
    EXPECT_FALSE(valid_lat(-91.0));
    EXPECT_TRUE(valid_lat(90.0));
    EXPECT_TRUE(valid_lat(-90.0));
    EXPECT_TRUE(valid_lat(0.0));
}

TEST(Validation, LngBoundary) {
    EXPECT_FALSE(valid_lng(181.0));
    EXPECT_FALSE(valid_lng(-181.0));
    EXPECT_TRUE(valid_lng(180.0));
    EXPECT_TRUE(valid_lng(-180.0));
}

TEST(Validation, RadiusDefault) { EXPECT_DOUBLE_EQ(clamp_radius(0),    500.0); }
TEST(Validation, RadiusMin)     { EXPECT_DOUBLE_EQ(clamp_radius(10),   50.0); }
TEST(Validation, RadiusMax)     { EXPECT_DOUBLE_EQ(clamp_radius(9999), 2000.0); }
TEST(Validation, RadiusPassthrough) { EXPECT_DOUBLE_EQ(clamp_radius(750), 750.0); }

TEST(Validation, LimitDefault) { EXPECT_EQ(clamp_limit(0),   20); }
TEST(Validation, LimitMin)     { EXPECT_EQ(clamp_limit(-1),  1); }
TEST(Validation, LimitMax)     { EXPECT_EQ(clamp_limit(500), 100); }
TEST(Validation, LimitPassthrough) { EXPECT_EQ(clamp_limit(50), 50); }
