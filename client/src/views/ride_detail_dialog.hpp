#pragma once

#include "backend_client.hpp"
#include "map_bridge.hpp"

#include <QDialog>
#include <QWebEngineView>
#include <QTimer>
#include <QLabel>
#include <QPushButton>

namespace bike::client {

class RideDetailDialog : public QDialog {
    Q_OBJECT
public:
    RideDetailDialog(BackendClient* client, const QString& token,
                     const QString& ride_no, QWidget* parent = nullptr);

private slots:
    void on_detail_loaded();
    void on_play_pause();
    void on_replay_tick();

private:
    BackendClient*  client_;
    QString         token_;
    QString         ride_no_;
    QWebEngineView* web_{nullptr};
    MapBridge*      bridge_{nullptr};
    QLabel*         lb_meta_{nullptr};
    QPushButton*    btn_play_{nullptr};
    QLabel*         lb_progress_{nullptr};
    QTimer          timer_;

    tutorial::get_ride_detail_response detail_;
    int                replay_idx_ = 0;
    bool               playing_ = false;
};

} // namespace bike::client
