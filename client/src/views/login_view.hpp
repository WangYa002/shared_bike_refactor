#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include <memory>

#include "backend_client.hpp"
#include "session_model.hpp"

namespace bike::client {

class LoginView : public QWidget {
    Q_OBJECT
public:
    LoginView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

signals:
    void logged_in();

private slots:
    void on_get_code();
    void on_login();

private:
    void set_status(const QString& text, bool is_err = false, bool is_ok = false);

    BackendClient* client_;
    SessionModel* session_;
    QLineEdit* le_mobile_{nullptr};
    QLineEdit* le_code_{nullptr};
    QPushButton* btn_get_code_{nullptr};
    QPushButton* btn_login_{nullptr};
    QLabel* lb_status_{nullptr};
};

} // namespace bike::client
