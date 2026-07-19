#include "bike/validation.hpp"

namespace bike {

bool valid_bike_no(const std::string& s) {
    return !s.empty() && s.size() <= 32;
}

bool valid_lat(double lat) {
    return lat >= -90.0 && lat <= 90.0;
}

bool valid_lng(double lng) {
    return lng >= -180.0 && lng <= 180.0;
}

double clamp_radius(double r) {
    if (r <= 0.0)   return 500.0;
    if (r < 50.0)   return 50.0;
    if (r > 2000.0) return 2000.0;
    return r;
}

// Option A: 0 → default (client omitted), negative → min (likely bug).
int clamp_limit(int n) {
    if (n == 0)   return 20;
    if (n < 1)    return 1;
    if (n > 100)  return 100;
    return n;
}

} // namespace bike
