#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "trajectory_sim.hpp"

#include <bike/geo.hpp>

#include <cmath>
#include <random>

namespace bike::client {

namespace {
constexpr double kMinSpeedKmh = 8.0;
constexpr double kMaxSpeedKmh = 15.0;
constexpr double kBearingJitterDeg = 15.0;
constexpr double kEarthRadiusM = 6371000.0;

// 给定起点、方位、距离,算新点。基于球面近似。
QPointF advance(double lat_deg, double lng_deg, double bearing_deg, double dist_m) {
    double lat1 = lat_deg * M_PI / 180.0;
    double lng1 = lng_deg * M_PI / 180.0;
    double brg  = bearing_deg * M_PI / 180.0;
    double dr   = dist_m / kEarthRadiusM;

    double lat2 = std::asin(std::sin(lat1) * std::cos(dr) +
                            std::cos(lat1) * std::sin(dr) * std::cos(brg));
    double lng2 = lng1 + std::atan2(std::sin(brg) * std::sin(dr) * std::cos(lat1),
                                    std::cos(dr) - std::sin(lat1) * std::sin(lat2));
    return QPointF(lat2 * 180.0 / M_PI, lng2 * 180.0 / M_PI);
}
} // namespace

TrajectorySim::TrajectorySim(std::uint32_t seed) : seed_(seed) {}

void TrajectorySim::start(double lat0, double lng0) {
    std::mt19937 rng(seed_);
    std::uniform_real_distribution<double> spd(kMinSpeedKmh, kMaxSpeedKmh);
    std::uniform_real_distribution<double> brg(0.0, 360.0);
    speed_mps_   = spd(rng) * 1000.0 / 3600.0;
    bearing_deg_ = brg(rng);
    current_     = QPointF(lat0, lng0);
    distance_m_  = 0.0;
    seq_         = 0;
    started_     = true;
}

QPointF TrajectorySim::step() {
    if (!started_) return current_;

    std::mt19937 rng(seed_ + static_cast<std::uint32_t>(seq_) + 1);
    std::uniform_real_distribution<double> jit(-kBearingJitterDeg, kBearingJitterDeg);
    std::uniform_real_distribution<double> spd(kMinSpeedKmh, kMaxSpeedKmh);
    bearing_deg_ = std::fmod(bearing_deg_ + jit(rng) + 360.0, 360.0);
    speed_mps_   = spd(rng) * 1000.0 / 3600.0;

    QPointF next = advance(current_.x(), current_.y(), bearing_deg_, speed_mps_);

    double seg = bike::haversine_m(current_.x(), current_.y(), next.x(), next.y());
    distance_m_ += seg;
    current_ = next;
    ++seq_;
    return current_;
}

} // namespace bike::client
