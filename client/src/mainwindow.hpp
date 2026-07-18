#pragma once

#include <QMainWindow>

#include "backend_client.hpp"
#include "session_model.hpp"
#include "views/login_view.hpp"
#include "views/wallet_view.hpp"
#include "views/records_view.hpp"

class QTabWidget;

namespace bike::client {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    BackendClient client_;
    SessionModel session_;
    QTabWidget* tabs_{nullptr};
    LoginView*  login_{nullptr};
    WalletView* wallet_{nullptr};
    RecordsView* records_{nullptr};
};

} // namespace bike::client
