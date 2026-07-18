#include "wallet_view.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QtConcurrent/QtConcurrent>
#include <QVBoxLayout>

namespace bike::client {

WalletView::WalletView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);

    lb_balance_ = new QLabel("Balance: ?", this);
    v->addWidget(lb_balance_);

    auto* h = new QHBoxLayout;
    le_amount_ = new QLineEdit(this);
    le_amount_->setPlaceholderText("amount in cents");
    btn_recharge_ = new QPushButton("Recharge", this);
    h->addWidget(le_amount_);
    h->addWidget(btn_recharge_);
    v->addLayout(h);

    btn_refresh_ = new QPushButton("Refresh balance", this);
    v->addWidget(btn_refresh_);

    lb_status_ = new QLabel("", this);
    v->addWidget(lb_status_);
    v->addStretch();

    connect(btn_recharge_, &QPushButton::clicked, this, &WalletView::on_recharge);
    connect(btn_refresh_, &QPushButton::clicked, this, &WalletView::refresh_balance);
}

void WalletView::refresh_balance() {
    if (!session_->logged_in()) {
        lb_status_->setText("not logged in");
        return;
    }
    lb_status_->setText("querying...");
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->get_balance(session_->token);
            QMetaObject::invokeMethod(this, [this, rsp] {
                lb_balance_->setText(QString("Balance: %1").arg(rsp.balance()));
                lb_status_->setText("");
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString::fromStdString(msg));
            });
        }
    });
}

void WalletView::on_recharge() {
    int amount = le_amount_->text().toInt();
    if (amount <= 0) {
        lb_status_->setText("amount must be positive");
        return;
    }
    lb_status_->setText("recharging...");
    QtConcurrent::run([this, amount] {
        try {
            auto rsp = client_->recharge(session_->token, amount);
            QMetaObject::invokeMethod(this, [this, rsp] {
                lb_balance_->setText(QString("Balance: %1").arg(rsp.balance()));
                lb_status_->setText("recharge ok");
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString::fromStdString(msg));
            });
        }
    });
}

} // namespace bike::client
