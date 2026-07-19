#include "wallet_view.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

namespace {

// 余额单位是分,展示成 元(保留两位小数)
QString format_yuan(int cents) {
    double yuan = cents / 100.0;
    return QString("¥ %L1").arg(yuan, 0, 'f', 2);
}

} // namespace

WalletView::WalletView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    auto* title = new QLabel(QString::fromUtf8("我的钱包"), this);
    title->setObjectName("title");
    v->addWidget(title);

    auto* sub = new QLabel(QString::fromUtf8("账户余额"), this);
    sub->setObjectName("subtitle");
    v->addWidget(sub);

    lb_balance_ = new QLabel(QString::fromUtf8("¥ —"), this);
    lb_balance_->setObjectName("balance");
    v->addWidget(lb_balance_);

    v->addSpacing(8);

    auto* lb_hint = new QLabel(QString::fromUtf8("充值金额(元)"), this);
    lb_hint->setObjectName("subtitle");
    v->addWidget(lb_hint);

    auto* h = new QHBoxLayout;
    h->setSpacing(8);
    le_amount_ = new QLineEdit(this);
    le_amount_->setPlaceholderText(QString::fromUtf8("输入充值金额,例如 5.00"));
    le_amount_->setMaximumWidth(220);
    btn_recharge_ = new QPushButton(QString::fromUtf8("充值"), this);
    btn_recharge_->setMinimumHeight(36);
    btn_recharge_->setCursor(Qt::PointingHandCursor);
    h->addWidget(le_amount_);
    h->addWidget(btn_recharge_);
    h->addStretch();
    v->addLayout(h);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    btn_refresh_ = new QPushButton(QString::fromUtf8("刷新余额"), this);
    btn_refresh_->setProperty("variant", "secondary");
    btn_refresh_->setMinimumHeight(32);
    btn_refresh_->setCursor(Qt::PointingHandCursor);
    actions->addWidget(btn_refresh_);
    actions->addStretch();
    v->addLayout(actions);

    lb_status_ = new QLabel(QString(), this);
    lb_status_->setObjectName("status");
    v->addWidget(lb_status_);

    v->addStretch();

    connect(btn_recharge_, &QPushButton::clicked, this, &WalletView::on_recharge);
    connect(btn_refresh_, &QPushButton::clicked, this, &WalletView::refresh_balance);
}

void WalletView::refresh_balance() {
    if (!session_->logged_in()) {
        set_status(QString::fromUtf8("尚未登录"), true);
        return;
    }
    set_status(QString::fromUtf8("正在查询余额…"));
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->get_balance(session_->token);
            QMetaObject::invokeMethod(this, [this, bal = rsp.balance()] {
                lb_balance_->setText(format_yuan(bal));
                set_status(QString::fromUtf8("已更新"), false, true);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                set_status(QString::fromUtf8("查询失败:%1").arg(QString::fromStdString(msg)), true);
            });
        }
    });
}

void WalletView::on_recharge() {
    bool ok = false;
    double yuan = le_amount_->text().toDouble(&ok);
    if (!ok || yuan <= 0) {
        set_status(QString::fromUtf8("请输入有效金额"), true);
        return;
    }
    int cents = static_cast<int>(yuan * 100 + 0.5);
    if (cents <= 0) {
        set_status(QString::fromUtf8("金额必须大于 0"), true);
        return;
    }
    set_status(QString::fromUtf8("正在充值 %1 …").arg(format_yuan(cents)));
    QtConcurrent::run([this, cents] {
        try {
            auto rsp = client_->recharge(session_->token, cents);
            QMetaObject::invokeMethod(this, [this, bal = rsp.balance()] {
                lb_balance_->setText(format_yuan(bal));
                set_status(QString::fromUtf8("充值成功"), false, true);
                le_amount_->clear();
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                set_status(QString::fromUtf8("充值失败:%1").arg(QString::fromStdString(msg)), true);
            });
        }
    });
}

void WalletView::set_status(const QString& text, bool is_err, bool is_ok) {
    lb_status_->setText(text);
    lb_status_->setProperty("err", is_err);
    lb_status_->setProperty("ok",  is_ok);
    lb_status_->style()->unpolish(lb_status_);
    lb_status_->style()->polish(lb_status_);
}

} // namespace bike::client
