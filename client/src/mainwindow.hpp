#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QStackedWidget>
#include <QString>

#include "backend_client.hpp"
#include "session_model.hpp"
#include "views/login_view.hpp"
#include "views/wallet_view.hpp"
#include "views/records_view.hpp"
#include "views/map_view.hpp"
#include "views/ride_view.hpp"
#include "views/ride_history_view.hpp"
#include "views/ride_detail_dialog.hpp"

class QTabWidget;

namespace bike::client {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void on_logged_in();
    void on_unlock_requested(const QString& bike_no);
    void on_ride_ended(const QString& desc, int amount_cent, int balance_after);
    void on_view_detail(const QString& ride_no);

private:
    void enter_main_ui();
    void check_orphan_ride();

    BackendClient client_;
    SessionModel  session_;
    QStackedWidget* stack_{nullptr};
    QTabWidget*     tabs_{nullptr};
    LoginView*        login_{nullptr};
    MapView*          map_view_{nullptr};
    RideView*         ride_view_{nullptr};
    WalletView*       wallet_{nullptr};
    RecordsView*      records_{nullptr};
    RideHistoryView*  history_{nullptr};

    QString active_ride_;
};

} // namespace bike::client
