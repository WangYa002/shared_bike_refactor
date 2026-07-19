#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include "backend_client.hpp"
#include "session_model.hpp"

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
    void set_status(const QString& text, bool is_err = false, bool is_ok = false);

    BackendClient* client_;
    SessionModel* session_;
    QLabel* lb_balance_{nullptr};
    QLineEdit* le_amount_{nullptr};
    QPushButton* btn_recharge_{nullptr};
    QPushButton* btn_refresh_{nullptr};
    QLabel* lb_status_{nullptr};
};

} // namespace bike::client
