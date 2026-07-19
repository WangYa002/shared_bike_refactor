#include <bike/pricing.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(Pricing, ZeroDurationIsBaseFee)       { EXPECT_EQ(compute_fee(0),       100); }
TEST(Pricing, AtExactly15MinStillBase)     { EXPECT_EQ(compute_fee(15*60),   100); }
TEST(Pricing, JustOver15MinChargesOneStep) { EXPECT_EQ(compute_fee(15*60+1), 150); }
TEST(Pricing, AtExactly30MinTwoBuckets)    { EXPECT_EQ(compute_fee(30*60),   150); }
TEST(Pricing, JustOver30MinThreeBuckets)   { EXPECT_EQ(compute_fee(30*60+1), 200); }
TEST(Pricing, AtExactly45Min)              { EXPECT_EQ(compute_fee(45*60),   200); }
TEST(Pricing, AtExactly60Min)              { EXPECT_EQ(compute_fee(60*60),   250); }
TEST(Pricing, FractionalSecRoundsUp)       { EXPECT_EQ(compute_fee(15*60+30),150); }
TEST(Pricing, NegativeInputRejected) {
    EXPECT_THROW(compute_fee(-1), std::invalid_argument);
}
