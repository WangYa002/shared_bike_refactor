#include <gtest/gtest.h>
#include "trajectory_sim.hpp"

#include <cmath>
#include <vector>

using bike::client::TrajectorySim;

TEST(TrajectorySim, SameSeedSameTrajectory) {
    TrajectorySim a(42);
    a.start(39.9821, 116.3145);
    TrajectorySim b(42);
    b.start(39.9821, 116.3145);

    for (int i = 0; i < 60; ++i) {
        auto pa = a.step();
        auto pb = b.step();
        EXPECT_FLOAT_EQ(pa.x(), pb.x());
        EXPECT_FLOAT_EQ(pa.y(), pb.y());
    }
}

TEST(TrajectorySim, DistanceGrowsMonotonically) {
    TrajectorySim t(7);
    t.start(39.9821, 116.3145);
    double prev = 0.0;
    for (int i = 0; i < 60; ++i) {
        t.step();
        double d = t.distance_m();
        EXPECT_GE(d, prev - 1e-6);
        prev = d;
    }
}

TEST(TrajectorySim, OneMinuteDistanceInPlausibleRange) {
    // 60 sec × 8-15 km/h × (1000/3600) m/s = 60 × [2.22, 4.17] = [133, 250]
    TrajectorySim t(123);
    t.start(39.9821, 116.3145);
    for (int i = 0; i < 60; ++i) t.step();
    double d = t.distance_m();
    EXPECT_GE(d, 100.0);
    EXPECT_LE(d, 280.0);
}

TEST(TrajectorySim, DifferentSeedDifferentTrajectory) {
    TrajectorySim a(1);
    a.start(39.9821, 116.3145);
    TrajectorySim b(2);
    b.start(39.9821, 116.3145);
    std::vector<QPointF> aa, bb;
    for (int i = 0; i < 10; ++i) { aa.push_back(a.step()); bb.push_back(b.step()); }
    bool any_diff = false;
    for (int i = 0; i < 10; ++i) {
        if (std::fabs(aa[i].x() - bb[i].x()) > 1e-9 ||
            std::fabs(aa[i].y() - bb[i].y()) > 1e-9) {
            any_diff = true; break;
        }
    }
    EXPECT_TRUE(any_diff);
}
