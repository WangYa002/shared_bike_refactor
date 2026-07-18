#pragma once

#include <QWidget>

#include "backend_client.hpp"
#include "session_model.hpp"

class QLineEdit;
class QLabel;
class QPushButton;

namespace bike::client {

class WalletView : public QWidget {
    Q_OBJECT
public:
    WalletView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

public slots:
    void refresh_balance();

private slots:
    void on_recharge();

private:
    BackendClient* client_;
    SessionModel* session_;
    QLabel* lb_balance_{nullptr};
    QLineEdit* le_amount_{nullptr};
    QPushButton* btn_recharge_{nullptr};
    QPushButton* btn_refresh_{nullptr};
    QLabel* lb_status_{nullptr};
};

} // namespace bike::client
