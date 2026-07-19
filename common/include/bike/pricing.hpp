#pragma once

namespace bike {

// 起步 1 元(100 分)含 15 分钟,之后每 15 分钟 0.5 元(50 分),向上取整。
// duration_sec < 0 抛 std::invalid_argument。
int compute_fee(int duration_sec);

} // namespace bike
