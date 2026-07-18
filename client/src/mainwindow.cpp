#include "mainwindow.hpp"

#include <QTabWidget>

namespace bike::client {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      client_("124.220.92.243", 8888) {
    tabs_ = new QTabWidget(this);
    login_   = new LoginView(&client_, &session_, this);
    wallet_  = new WalletView(&client_, &session_, this);
    records_ = new RecordsView(&client_, &session_, this);
    tabs_->addTab(login_,   "Login");
    tabs_->addTab(wallet_,  "Wallet");
    tabs_->addTab(records_, "Records");
    setCentralWidget(tabs_);

    connect(login_, &LoginView::logged_in, this, [this] {
        wallet_->refresh_balance();
        records_->refresh();
        tabs_->setCurrentIndex(1);
    });

    setWindowTitle("Shared Bike Client");
    resize(640, 480);
}

} // namespace bike::client
