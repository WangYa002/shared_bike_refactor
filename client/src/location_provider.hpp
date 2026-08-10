#pragma once

#include <QObject>
#include <QString>
#include <functional>

class QGeoPositionInfoSource;
class QTimer;

namespace bike::client {

// 常驻定位提供者: 构造时创建一次 QGeoPositionInfoSource 并 startUpdates()
// 周期(2s)推送, 每次定位成功 emit positionReady(lat, lng)。
// 旧实现是"每次新建 source 单次请求"模型, 导致定位不实时; 已改为订阅模型。
//
// 错误处理: connect errorOccurred, 区分 AccessDenied(停止更新并暴露状态)与
// UpdateTimeout(仅提示, 保留周期更新不销毁 source)。首次定位超时约 8 秒。
//
// QML 侧(context property "location")用 requestOnce() + positionReady +
// status 属性; request_once 回调形式保留供 C++ 侧使用。
class LocationProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    using Callback = std::function<void(double lat, double lng)>;

    explicit LocationProvider(QObject* parent = nullptr);

    QString status() const { return status_; }

    // 异步请求一次当前位置。回调在主线程触发。
    // 已有缓存定位 → 立即回调缓存; 无定位源/超时 → 回调默认坐标。
    void request_once(Callback cb);

    // QML 入口: 结果经 positionReady 信号送达。
    // 有缓存定位时立即推送一次最新缓存, 同时强制请求一次新的更新。
    Q_INVOKABLE void requestOnce();

    static double default_lat() { return 39.9821; }
    static double default_lng() { return 116.3145; }

signals:
    void positionReady(double lat, double lng);
    // 定位异常描述(权限被拒/服务不可用等), 供 QML 提示。
    void locationError(QString message);
    void statusChanged();

private:
    void set_status(const QString& s);

    QGeoPositionInfoSource* src_{nullptr};
    QTimer* first_fix_timer_{nullptr};   // 首次定位超时看护(约 8s)
    QString status_{"初始化"};
    double last_lat_{default_lat()};
    double last_lng_{default_lng()};
    bool has_fix_{false};

    static constexpr int kUpdateIntervalMs = 2000;
    static constexpr int kFirstFixTimeoutMs = 8000;
};

} // namespace bike::client
