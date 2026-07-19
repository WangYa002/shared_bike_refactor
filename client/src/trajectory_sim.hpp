#pragma once

#include <QPointF>
#include <cstdint>

namespace bike::client {

// 本地模拟的骑行轨迹生成器。
// - 用 std::mt19937 + seed,deterministic(同 seed + 同起点 → 完全相同的轨迹)
// - 速度模型:8–15 km/h(典型共享单车)
// - 方向模型:每秒抖动 ±15°,保持大致直线
// - distance_m 用 Haversine 累加
class TrajectorySim {
public:
    explicit TrajectorySim(std::uint32_t seed);

    // 起点。reset 内部状态(distance_m = 0,seq = 0)。
    void start(double lat0, double lng0);

    // 推进一步(每秒调用一次)。返回新坐标点。
    // 调用前必须 start(),否则行为未定义。
    QPointF step();

    QPointF current() const { return current_; }
    double  distance_m() const { return distance_m_; }
    int     seq() const { return seq_; }

private:
    std::uint32_t seed_;
    double        speed_mps_;   // 当前速度,米/秒
    double        bearing_deg_; // 当前方位,0=北,顺时针
    QPointF       current_;
    double        distance_m_ = 0.0;
    int           seq_ = 0;
    bool          started_ = false;
};

} // namespace bike::client
