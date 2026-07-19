#pragma once

#include <string>

namespace bike {

// date_yyyymmdd 例 20260719;daily_seq 1..999999
// 返回 "R" + 8 位日期 + 6 位零填充序号,例 "R20260719000001"
// daily_seq 超出 [1, 999999] 抛 std::invalid_argument
std::string make_ride_no(int date_yyyymmdd, int daily_seq);

} // namespace bike
