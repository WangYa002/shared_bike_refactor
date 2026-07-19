#include "login_view.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

LoginView::LoginView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(8);

    auto* title = new QLabel(QString::fromUtf8("欢迎回来"), this);
    title->setObjectName("title");
    v->addWidget(title);

    auto* subtitle = new QLabel(QString::fromUtf8("输入手机号获取验证码,登录后即可使用钱包和查看账单"), this);
    subtitle->setObjectName("subtitle");
    subtitle->setWordWrap(true);
    v->addWidget(subtitle);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setSpacing(10);

    le_mobile_ = new QLineEdit(this);
    le_mobile_->setPlaceholderText(QString::fromUtf8("11 位手机号,例如 15600000010"));
    le_mobile_->setMaxLength(11);
    le_code_  = new QLineEdit(this);
    le_code_->setPlaceholderText(QString::fromUtf8("6 位验证码"));
    le_code_->setMaxLength(6);

    form->addRow(QString::fromUtf8("手机号"), le_mobile_);
    form->addRow(QString::fromUtf8("验证码"), le_code_);
    v->addLayout(form);

    btn_get_code_ = new QPushButton(QString::fromUtf8("获取验证码"), this);
    btn_login_    = new QPushButton(QString::fromUtf8("登录"), this);
    btn_get_code_->setMinimumHeight(36);
    btn_login_->setMinimumHeight(40);

    auto* btns = new QVBoxLayout;
    btns->setSpacing(8);
    btns->addWidget(btn_login_);
    btns->addWidget(btn_get_code_);
    v->addLayout(btns);

    lb_status_ = new QLabel(QString::fromUtf8("请先获取验证码"), this);
    lb_status_->setObjectName("status");
    v->addWidget(lb_status_);

    v->addStretch();

    connect(btn_get_code_, &QPushButton::clicked, this, &LoginView::on_get_code);
    connect(btn_login_,    &QPushButton::clicked, this, &LoginView::on_login);
}

void LoginView::on_get_code() {
    auto mobile = le_mobile_->text().toStdString();
    if (mobile.empty()) {
        set_status(QString::fromUtf8("请先输入手机号"), true);
        return;
    }
    set_status(QString::fromUtf8("正在发送验证码…"), false);
    btn_get_code_->setEnabled(false);

    QtConcurrent::run([this, mobile] {
        try {
            auto rsp = client_->get_mobile_code(mobile);
            QMetaObject::invokeMethod(this, [this, icode = rsp.icode()] {
                set_status(QString::fromUtf8("验证码已发送:%1(测试环境直接显示)")
                               .arg(icode, 6, 10, QChar('0')), false);
                btn_get_code_->setEnabled(true);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                set_status(QString::fromUtf8("发送失败:%1").arg(QString::fromStdString(msg)), true);
                btn_get_code_->setEnabled(true);
            });
        }
    });
}

void LoginView::on_login() {
    auto mobile = le_mobile_->text().toStdString();
    int code = le_code_->text().toInt();
    if (mobile.empty() || code == 0) {
        set_status(QString::fromUtf8("请填写手机号和验证码"), true);
        return;
    }
    set_status(QString::fromUtf8("正在登录…"), false);
    btn_login_->setEnabled(false);

    QtConcurrent::run([this, mobile, code] {
        try {
            auto rsp = client_->login(mobile, code);
            QMetaObject::invokeMethod(this, [this, rsp, mobile] {
                btn_login_->setEnabled(true);
                if (rsp.code() == 200 && !rsp.session_token().empty()) {
                    session_->mobile = mobile;
                    session_->token  = rsp.session_token();
                    set_status(QString::fromUtf8("登录成功,跳转到钱包…"), false, true);
                    emit logged_in();
                } else {
                    set_status(QString::fromUtf8("登录失败:%1")
                        .arg(QString::fromStdString(rsp.desc())), true);
                }
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                set_status(QString::fromUtf8("登录异常:%1").arg(QString::fromStdString(msg)), true);
                btn_login_->setEnabled(true);
            });
        }
    });
}

void LoginView::set_status(const QString& text, bool is_err, bool is_ok) {
    lb_status_->setText(text);
    lb_status_->setProperty("err", is_err);
    lb_status_->setProperty("ok",  is_ok);
    lb_status_->style()->unpolish(lb_status_);
    lb_status_->style()->polish(lb_status_);
}

} // namespace bike::client
