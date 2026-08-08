#include "trajectory_model.hpp"

namespace bike::client {

TrajectoryModel::TrajectoryModel(QObject* parent) : QObject(parent), sim_(0) {
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &TrajectoryModel::onTick);
}

void TrajectoryModel::start(const QString& ride_no, double lat, double lng,
                            std::uint32_t seed) {
    // 幂等: 重复 start 先停表复位, 避免双 timer/旧状态残留。
    timer_.stop();
    active_ = false;
    ride_no_ = ride_no;
    elapsed_sec_ = 0;
    sim_ = TrajectorySim(seed);
    sim_.start(lat, lng);
    active_ = true;
    emit stateChanged();
    timer_.start();
}

void TrajectoryModel::stop() {
    if (!active_) return;
    timer_.stop();
    active_ = false;
    emit stateChanged();
}

void TrajectoryModel::onTick() {
    if (!active_) return;
    ++elapsed_sec_;
    const QPointF p = sim_.step();
    emit tick(p.x(), p.y(), sim_.seq(), elapsed_sec_, sim_.distance_m());
}

} // namespace bike::client
