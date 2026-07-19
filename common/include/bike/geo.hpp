#pragma once

namespace bike {

// WGS84 椭球大圆距离,单位米。两点重合返回 0。
double haversine_m(double lat1, double lng1, double lat2, double lng2);

// 中国大陆境内粗略判断(用于拒绝明显错误的坐标)。
// 范围:lat 18–54,lng 73–135。
bool is_in_china(double lat, double lng);

} // namespace bike
