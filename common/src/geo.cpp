#include "bike/geo.hpp"

#include <cmath>

namespace bike {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371000.0;

double to_rad(double deg) { return deg * kPi / 180.0; }
}

double haversine_m(double lat1, double lng1, double lat2, double lng2) {
    double la1 = to_rad(lat1), la2 = to_rad(lat2);
    double dla = to_rad(lat2 - lat1);
    double dlo = to_rad(lng2 - lng1);
    double a = std::sin(dla / 2) * std::sin(dla / 2) +
               std::cos(la1) * std::cos(la2) *
               std::sin(dlo / 2) * std::sin(dlo / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return kEarthRadiusM * c;
}

bool is_in_china(double lat, double lng) {
    return lat >= 18.0 && lat <= 54.0 && lng >= 73.0 && lng <= 135.0;
}

} // namespace bike
