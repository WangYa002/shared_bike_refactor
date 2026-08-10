#include "location_provider.hpp"

#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QMetaObject>
#include <QTimer>

#include <QDebug>

#include <memory>

namespace bike::client {

LocationProvider::LocationProvider(QObject* parent) : QObject(parent) {
    // 常驻模式: 定位源只创建一次, 全程复用。
    src_ = QGeoPositionInfoSource::createDefaultSource(this);
    if (!src_) {
        qWarning() << "LocationProvider: 无可用定位源"
                      "(Windows 需确认系统定位服务已开启)";
        set_status("定位不可用");
        emit locationError("定位服务不可用, 使用默认坐标");
        return;
    }

    // 首次定位超时看护: 约 8 秒未拿到定位则提示超时,
    // 但保留周期更新, 不销毁 source(后续仍可能定位成功)。
    first_fix_timer_ = new QTimer(this);
    first_fix_timer_->setSingleShot(true);
    connect(first_fix_timer_, &QTimer::timeout, this, [this] {
        if (has_fix_) return;
        qWarning() << "LocationProvider: 首次定位超时(8s), 继续周期更新";
        set_status("定位超时");
        emit locationError("首次定位超时, 仍在持续尝试");
    });

    connect(src_, &QGeoPositionInfoSource::positionUpdated, this,
            [this](const QGeoPositionInfo& info) {
                if (!info.isValid()) return;
                const auto coord = info.coordinate();
                last_lat_ = coord.latitude();
                last_lng_ = coord.longitude();
                has_fix_ = true;
                if (first_fix_timer_) first_fix_timer_->stop();
                set_status("已定位");
                emit positionReady(last_lat_, last_lng_);
            });

    connect(src_, &QGeoPositionInfoSource::errorOccurred, this,
            [this](QGeoPositionInfoSource::Error err) {
                switch (err) {
                case QGeoPositionInfoSource::AccessError:
                    // 权限被拒: 不再有意义, 停止更新并明确暴露给 QML。
                    qWarning() << "LocationProvider: 定位权限被拒绝";
                    if (src_) src_->stopUpdates();
                    set_status("定位被拒绝");
                    emit locationError("定位权限被拒绝");
                    break;
                case QGeoPositionInfoSource::UpdateTimeoutError:
                    // 超时: 仅提示, 保留周期更新等待下一次定位。
                    qWarning() << "LocationProvider: 定位更新超时";
                    if (!has_fix_) set_status("定位超时");
                    break;
                default:
                    qWarning() << "LocationProvider: 定位错误 code=" << int(err);
                    emit locationError("定位异常");
                    break;
                }
            });

    src_->setUpdateInterval(kUpdateIntervalMs);
    src_->startUpdates();
    set_status("定位中");
    first_fix_timer_->start(kFirstFixTimeoutMs);
}

void LocationProvider::set_status(const QString& s) {
    if (status_ == s) return;
    status_ = s;
    emit statusChanged();
}

void LocationProvider::requestOnce() {
    // 有缓存定位: 立即把最新位置推给 QML(定位按钮即时响应)。
    if (has_fix_) emit positionReady(last_lat_, last_lng_);
    // 强制请求一次新的更新, 结果经 positionUpdated → positionReady 送达。
    if (src_) src_->requestUpdate(kFirstFixTimeoutMs);
}

void LocationProvider::request_once(Callback cb) {
    // 有缓存定位: 立即回调。
    if (has_fix_) {
        cb(last_lat_, last_lng_);
        return;
    }
    // 无定位源: 回退默认坐标。
    if (!src_) {
        cb(default_lat(), default_lng());
        return;
    }
    // 等待常驻流的下一个定位点; 超时回退默认坐标。done 标志防双回调。
    auto done = std::make_shared<bool>(false);
    auto conn = std::make_shared<QMetaObject::Connection>();
    auto finish = [this, cb, done, conn](double lat, double lng) {
        if (*done) return;
        *done = true;
        disconnect(*conn);
        cb(lat, lng);
    };
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this,
            [finish]() { finish(default_lat(), default_lng()); });
    connect(timer, &QTimer::timeout, timer, &QObject::deleteLater);
    *conn = connect(src_, &QGeoPositionInfoSource::positionUpdated, this,
                    [finish](const QGeoPositionInfo& info) {
                        if (!info.isValid()) return;
                        const auto coord = info.coordinate();
                        finish(coord.latitude(), coord.longitude());
                    });
    timer->start(kFirstFixTimeoutMs);
    src_->requestUpdate(kFirstFixTimeoutMs);
}

} // namespace bike::client
