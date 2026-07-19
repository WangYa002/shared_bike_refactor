#pragma once

#include "backend_client.hpp"
#include "location_provider.hpp"
#include "map_bridge.hpp"
#include "session_model.hpp"

#include <QWebEngineView>
#include <QWidget>

class QPushButton;
class QLabel;

namespace bike::client {

class MapView : public QWidget {
    Q_OBJECT
public:
    MapView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

    void refresh();
    void set_status(const QString& s) { lb_status_->setText(s); }

    double my_lat() const { return my_lat_; }
    double my_lng() const { return my_lng_; }

signals:
    void unlockRequested(const QString& bike_no);
    void userLocationUpdated(double lat, double lng);

private slots:
    void onLoadFinished(bool ok);
    void onBikeClicked(const QString& bike_no);

private:
    BackendClient*   client_;
    SessionModel*    session_;
    QWebEngineView*  web_{nullptr};
    MapBridge*       bridge_{nullptr};
    LocationProvider loc_{nullptr};
    QPushButton*     btn_refresh_{nullptr};
    QPushButton*     btn_locate_{nullptr};
    QLabel*          lb_status_{nullptr};
    double           my_lat_ = LocationProvider::default_lat();
    double           my_lng_ = LocationProvider::default_lng();
};

} // namespace bike::client
