#pragma once

#include <string>

namespace bike {

bool valid_bike_no(const std::string& s);   // 1..32 字节,非空
bool valid_lat(double lat);                  // [-90, 90]
bool valid_lng(double lng);                  // [-180, 180]
double clamp_radius(double r);               // 默认 500,clamp 到 [50, 2000]
int    clamp_limit(int n);                   // 默认 20,clamp 到 [1, 100]

} // namespace bike
