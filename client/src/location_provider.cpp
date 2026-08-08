#include "location_provider.hpp"

#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QTimer>
#include <QMetaObject>

#include <memory>

namespace bike::client {

LocationProvider::LocationProvider(QObject* parent) : QObject(parent) {}

void LocationProvider::requestOnce() {
    request_once([this](double lat, double lng) { emit positionReady(lat, lng); });
}

void LocationProvider::request_once(Callback cb) {
    auto* src = QGeoPositionInfoSource::createDefaultSource(this);
    if (!src) {
        cb(default_lat(), default_lng());
        return;
    }
    src->setUpdateInterval(0);  // 单次

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);

    // #6: done 标志防止超时与迟到定位双回调 —— 先到者回调一次并拆场,
    // 后到者直接忽略。
    auto done = std::make_shared<bool>(false);
    auto finish = [this, cb, src, timer, done](double lat, double lng) {
        if (*done) return;
        *done = true;
        timer->stop();
        src->stopUpdates();
        src->disconnect(this);
        cb(lat, lng);
        src->deleteLater();
        timer->deleteLater();
    };

    QObject::connect(src, &QGeoPositionInfoSource::positionUpdated,
                     this, [finish](const QGeoPositionInfo& info) {
        if (info.isValid()) {
            const auto coord = info.coordinate();
            finish(coord.latitude(), coord.longitude());
        } else {
            finish(default_lat(), default_lng());
        }
    });

    QObject::connect(timer, &QTimer::timeout, this,
                     [finish]() { finish(default_lat(), default_lng()); });

    timer->start(2000);
    src->requestUpdate(2000);
}

} // namespace bike::client
