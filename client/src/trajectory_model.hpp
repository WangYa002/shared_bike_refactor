#pragma once

#include "trajectory_sim.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>

namespace bike::client {

// TrajectorySim 的 QObject 包装 (context property "trajectory")。
// GUI 线程 QTimer 每秒 tick: 推进一步轨迹, emit tick(...)。
// QML 侧在 onTick 里驱动地图平移 (map.js appendTrajectory) 与
// api.reportPosition 位置上报 —— 纯信号槽, 不再需要分离线程。
class TrajectoryModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(QString rideNo READ rideNo NOTIFY stateChanged)
    Q_PROPERTY(double currentLat READ currentLat NOTIFY tick)
    Q_PROPERTY(double currentLng READ currentLng NOTIFY tick)
public:
    explicit TrajectoryModel(QObject* parent = nullptr);

    bool active() const { return active_; }
    QString rideNo() const { return ride_no_; }
    double currentLat() const { return sim_.current().x(); }
    double currentLng() const { return sim_.current().y(); }
    double distanceM() const { return sim_.distance_m(); }

    // 开始模拟骑行 (旧 RideView::start_ride 语义)。
    Q_INVOKABLE void start(const QString& ride_no, double lat, double lng, quint32 seed);
    Q_INVOKABLE void stop();

signals:
    void stateChanged();
    // 每秒一步: 新坐标 / 轨迹序号 / 已骑行秒数 / 累计里程(米)。
    void tick(double lat, double lng, int seq, int elapsedSec, double distanceM);

private slots:
    void onTick();

private:
    QTimer        timer_;
    TrajectorySim sim_{0};
    QString       ride_no_;
    int           elapsed_sec_ = 0;
    bool          active_ = false;
};

} // namespace bike::client
