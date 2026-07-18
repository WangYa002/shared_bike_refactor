#include "login_view.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

LoginView::LoginView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* form = new QFormLayout(this);

    le_mobile_ = new QLineEdit(this);
    le_mobile_->setPlaceholderText("e.g. 15600000000");
    le_code_ = new QLineEdit(this);
    le_code_->setPlaceholderText("6-digit code");
    btn_get_code_ = new QPushButton("Get code", this);
    btn_login_ = new QPushButton("Login", this);
    lb_status_ = new QLabel("", this);

    form->addRow("Mobile:", le_mobile_);
    form->addRow("Code:", le_code_);
    form->addRow(btn_get_code_);
    form->addRow(btn_login_);
    form->addRow(lb_status_);

    connect(btn_get_code_, &QPushButton::clicked, this, &LoginView::on_get_code);
    connect(btn_login_,   &QPushButton::clicked, this, &LoginView::on_login);
}

void LoginView::on_get_code() {
    auto mobile = le_mobile_->text().toStdString();
    if (mobile.empty()) {
        lb_status_->setText("enter mobile first");
        return;
    }
    lb_status_->setText("sending code...");
    btn_get_code_->setEnabled(false);

    QtConcurrent::run([this, mobile] {
        try {
            auto rsp = client_->get_mobile_code(mobile);
            QMetaObject::invokeMethod(this, [this, rsp] {
                lb_status_->setText(QString("code sent (icode=%1)").arg(rsp.icode()));
                btn_get_code_->setEnabled(true);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString("error: %1").arg(QString::fromStdString(msg)));
                btn_get_code_->setEnabled(true);
            });
        }
    });
}

void LoginView::on_login() {
    auto mobile = le_mobile_->text().toStdString();
    int code = le_code_->text().toInt();
    if (mobile.empty() || code == 0) {
        lb_status_->setText("fill mobile + code");
        return;
    }
    lb_status_->setText("logging in...");
    btn_login_->setEnabled(false);

    QtConcurrent::run([this, mobile, code] {
        try {
            auto rsp = client_->login(mobile, code);
            QMetaObject::invokeMethod(this, [this, rsp, mobile] {
                btn_login_->setEnabled(true);
                if (rsp.code() == 200 && !rsp.session_token().empty()) {
                    session_->mobile = mobile;
                    session_->token  = rsp.session_token();
                    lb_status_->setText("login ok");
                    emit logged_in();
                } else {
                    lb_status_->setText(QString("login failed: %1")
                        .arg(QString::fromStdString(rsp.desc())));
                }
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString("error: %1").arg(QString::fromStdString(msg)));
                btn_login_->setEnabled(true);
            });
        }
    });
}

} // namespace bike::client
