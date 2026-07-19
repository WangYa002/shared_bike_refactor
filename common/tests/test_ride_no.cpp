#include <bike/ride_no.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(RideNo, FirstSeq) {
    EXPECT_EQ(make_ride_no(20260719, 1), "R20260719000001");
}

TEST(RideNo, MaxSeq) {
    EXPECT_EQ(make_ride_no(20260719, 999999), "R20260719999999");
}

TEST(RideNo, DifferentDate) {
    EXPECT_EQ(make_ride_no(20251231, 42), "R20251231000042");
}

TEST(RideNo, RejectsZeroSeq) {
    EXPECT_THROW(make_ride_no(20260719, 0), std::invalid_argument);
}

TEST(RideNo, RejectsOverflowSeq) {
    EXPECT_THROW(make_ride_no(20260719, 1000000), std::invalid_argument);
}
