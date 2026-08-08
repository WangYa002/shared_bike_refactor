#pragma once

#include <QObject>
#include <functional>

namespace bike::client {

// 用 Qt Positioning(QGeoPositionInfoSource)拿当前位置。
// 失败时回退到默认坐标(五道口中心)。
// QML 侧(context property "location")用 requestOnce() + positionReady 信号;
// request_once 回调形式保留供 C++ 侧使用。
class LocationProvider : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(double lat, double lng)>;

    explicit LocationProvider(QObject* parent = nullptr);

    // 异步请求一次当前位置。回调在主线程触发。
    // 失败/超时(2s)→ 回调默认坐标(39.9821, 116.3145)。
    void request_once(Callback cb);

    // QML 入口: 结果经 positionReady 信号送达。
    Q_INVOKABLE void requestOnce();

    static double default_lat() { return 39.9821; }
    static double default_lng() { return 116.3145; }

signals:
    void positionReady(double lat, double lng);
};

} // namespace bike::client
