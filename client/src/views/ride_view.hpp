#pragma once

#include "backend_client.hpp"
#include "session_model.hpp"
#include "trajectory_sim.hpp"

#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace bike::client {

class RideView : public QWidget {
    Q_OBJECT
public:
    RideView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

    void start_ride(const QString& ride_no, double start_lat, double start_lng,
                    std::uint32_t sim_seed);
    void end_ride_external();

signals:
    void ended(const QString& ride_no, int amount_cent, int balance_after);
    void positionUpdated(double lat, double lng);

private slots:
    void on_tick();
    void on_end_clicked();

private:
    BackendClient* client_;
    SessionModel*  session_;
    QLabel*  lb_title_{nullptr};
    QLabel*  lb_timer_{nullptr};
    QLabel*  lb_distance_{nullptr};
    QLabel*  lb_estimate_{nullptr};
    QLabel*  lb_status_{nullptr};
    QPushButton* btn_end_{nullptr};

    QTimer         timer_;
    TrajectorySim  sim_;
    QString        ride_no_;
    double         start_lat_ = 0.0;
    double         start_lng_ = 0.0;
    int            elapsed_sec_ = 0;
    bool           active_ = false;
};

} // namespace bike::client
