#include "map_view.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

MapView::MapView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    web_ = new QWebEngineView(this);
    auto* channel = new QWebChannel(web_->page());
    bridge_ = new MapBridge(this);
    channel->registerObject(QStringLiteral("bridge"), bridge_);
    web_->page()->setWebChannel(channel);
    web_->load(QUrl("qrc:/map.html"));
    v->addWidget(web_, 1);

    auto* overlay = new QWidget(this);
    overlay->setObjectName("mapOverlay");
    auto* h = new QHBoxLayout(overlay);
    h->setContentsMargins(12, 12, 12, 12);
    btn_refresh_ = new QPushButton(QString::fromUtf8("刷新车辆"), overlay);
    btn_refresh_->setProperty("variant", "secondary");
    btn_locate_  = new QPushButton(QString::fromUtf8("定位"), overlay);
    btn_locate_->setProperty("variant", "secondary");
    lb_status_   = new QLabel(QString::fromUtf8("正在加载地图…"), overlay);
    lb_status_->setObjectName("status");
    h->addWidget(lb_status_);
    h->addStretch();
    h->addWidget(btn_locate_);
    h->addWidget(btn_refresh_);
    overlay->setMaximumHeight(56);
    v->addWidget(overlay);

    connect(web_, &QWebEngineView::loadFinished, this, &MapView::onLoadFinished);
    connect(bridge_, &MapBridge::bikeClicked, this, &MapView::onBikeClicked);
    connect(btn_refresh_, &QPushButton::clicked, this, &MapView::refresh);
    connect(btn_locate_, &QPushButton::clicked, this, [this] {
        loc_.request_once([this](double lat, double lng) {
            my_lat_ = lat; my_lng_ = lng;
            emit userLocationUpdated(lat, lng);
            QString js = QString("setUserLocation(%1, %2);").arg(lat).arg(lng);
            web_->page()->runJavaScript(js);
        });
    });
}

void MapView::onLoadFinished(bool ok) {
    if (!ok) {
        lb_status_->setText(QString::fromUtf8("地图加载失败"));
        return;
    }
    QString js = QString("setUserLocation(%1, %2);")
                     .arg(LocationProvider::default_lat())
                     .arg(LocationProvider::default_lng());
    web_->page()->runJavaScript(js);
    refresh();
    loc_.request_once([this](double lat, double lng) {
        my_lat_ = lat; my_lng_ = lng;
        emit userLocationUpdated(lat, lng);
        QString js = QString("setUserLocation(%1, %2);").arg(lat).arg(lng);
        web_->page()->runJavaScript(js);
    });
}

void MapView::onBikeClicked(const QString& bike_no) {
    lb_status_->setText(QString::fromUtf8("已选车:%1").arg(bike_no));
    emit unlockRequested(bike_no);
}

void MapView::refresh() {
    if (!session_->logged_in()) {
        lb_status_->setText(QString::fromUtf8("尚未登录"));
        return;
    }
    lb_status_->setText(QString::fromUtf8("正在拉取附近车辆…"));
    double lat = my_lat_, lng = my_lng_;
    QtConcurrent::run([this, lat, lng] {
        try {
            auto rsp = client_->list_nearby_bikes(session_->token, lat, lng, 1000.0);
            QJsonArray arr;
            for (int i = 0; i < rsp.bikes_size(); ++i) {
                const auto& b = rsp.bikes(i);
                QJsonObject o;
                o["bike_no"] = QString::fromStdString(b.bike_no());
                o["lat"]     = b.lat();
                o["lng"]     = b.lng();
                o["status"]  = b.status();
                arr.append(o);
            }
            QString jsonStr = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
            QMetaObject::invokeMethod(this, [this, jsonStr, n = rsp.bikes_size()] {
                web_->page()->runJavaScript(
                    QString("renderBikes(%1);").arg(jsonStr));
                lb_status_->setText(QString::fromUtf8("共 %1 辆车").arg(n));
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString::fromUtf8("加载失败:%1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

} // namespace bike::client
