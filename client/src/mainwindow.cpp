#include "mainwindow.hpp"

#include <QFont>
#include <QTabWidget>

namespace bike::client {

namespace {

constexpr const char* kStyle = R"(
* {
    font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;
    font-size: 13px;
    color: #1f2937;
}

QMainWindow, QWidget {
    background: #f7f8fa;
}

QTabWidget::pane {
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    top: -1px;
    background: #ffffff;
    padding: 16px;
}

QTabBar::tab {
    background: #eef0f3;
    color: #4b5563;
    padding: 10px 24px;
    margin-right: 4px;
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
    min-width: 110px;
    font-weight: 500;
}

QTabBar::tab:selected {
    background: #ffffff;
    color: #2563eb;
    border: 1px solid #e5e7eb;
    border-bottom: 2px solid #2563eb;
}

QTabBar::tab:hover:!selected {
    background: #e3e6ea;
}

QLabel#title {
    font-size: 22px;
    font-weight: 600;
    color: #111827;
    padding: 4px 0 12px 0;
}

QLabel#subtitle {
    font-size: 12px;
    color: #6b7280;
    padding-bottom: 16px;
}

QLabel#balance {
    font-size: 32px;
    font-weight: 700;
    color: #2563eb;
    padding: 8px 0;
}

QLabel#status {
    color: #6b7280;
    font-size: 12px;
    padding: 4px 0;
}

QLabel#status[err="true"] {
    color: #dc2626;
}

QLabel#status[ok="true"] {
    color: #059669;
}

QLineEdit {
    padding: 8px 12px;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    background: #ffffff;
    selection-background-color: #2563eb;
}

QLineEdit:focus {
    border: 1px solid #2563eb;
}

QPushButton {
    background: #2563eb;
    color: #ffffff;
    border: none;
    border-radius: 6px;
    padding: 8px 20px;
    font-weight: 500;
}

QPushButton:hover {
    background: #1d4ed8;
}

QPushButton:pressed {
    background: #1e40af;
}

QPushButton:disabled {
    background: #9ca3af;
}

QPushButton[variant="secondary"] {
    background: #ffffff;
    color: #2563eb;
    border: 1px solid #2563eb;
}

QPushButton[variant="secondary"]:hover {
    background: #eff6ff;
}

QHeaderView::section {
    background: #f3f4f6;
    color: #374151;
    padding: 8px;
    border: none;
    border-bottom: 1px solid #e5e7eb;
    font-weight: 600;
}

QTableWidget {
    background: #ffffff;
    border: 1px solid #e5e7eb;
    border-radius: 6px;
    gridline-color: #f3f4f6;
    selection-background-color: #dbeafe;
    selection-color: #1e40af;
}

QTableWidget::item {
    padding: 6px 8px;
}

QTableWidget::item:selected {
    background: #dbeafe;
    color: #1e40af;
}
)";

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      client_("124.220.92.243", 8888) {
    setStyleSheet(QString::fromUtf8(kStyle));

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    login_   = new LoginView(&client_, &session_, this);
    wallet_  = new WalletView(&client_, &session_, this);
    records_ = new RecordsView(&client_, &session_, this);
    tabs_->addTab(login_,   QString::fromUtf8("登录"));
    tabs_->addTab(wallet_,  QString::fromUtf8("钱包"));
    tabs_->addTab(records_, QString::fromUtf8("账单"));
    setCentralWidget(tabs_);

    connect(login_, &LoginView::logged_in, this, [this] {
        wallet_->refresh_balance();
        records_->refresh();
        tabs_->setCurrentIndex(1);
    });

    setWindowTitle(QString::fromUtf8("共享单车 · 用户客户端"));
    resize(820, 560);
    tabs_->setCurrentIndex(0);
}

} // namespace bike::client
