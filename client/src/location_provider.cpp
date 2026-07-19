#include "location_provider.hpp"

#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QTimer>
#include <QMetaObject>

namespace bike::client {

LocationProvider::LocationProvider(QObject* parent) : QObject(parent) {}

void LocationProvider::request_once(Callback cb) {
    auto* src = QGeoPositionInfoSource::createDefaultSource(this);
    if (!src) {
        cb(default_lat(), default_lng());
        return;
    }
    src->setUpdateInterval(0);  // 单次

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);

    QObject::connect(src, &QGeoPositionInfoSource::positionUpdated,
                     this, [this, cb, src, timer](const QGeoPositionInfo& info) {
        timer->stop();
        if (info.isValid()) {
            auto coord = info.coordinate();
            cb(coord.latitude(), coord.longitude());
        } else {
            cb(default_lat(), default_lng());
        }
        src->deleteLater();
        timer->deleteLater();
    });

    QObject::connect(timer, &QTimer::timeout, this, [this, cb, src, timer]() {
        cb(default_lat(), default_lng());
        src->stopUpdates();
        src->deleteLater();
        timer->deleteLater();
    });

    timer->start(2000);
    src->requestUpdate(2000);
}

} // namespace bike::client
