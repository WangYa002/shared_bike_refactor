#include "mainwindow.hpp"

#include <QDateTime>
#include <QFont>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QtConcurrent/QtConcurrent>

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

QStackedWidget { background: #f7f8fa; }

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

QTabBar::tab:hover:!selected { background: #e3e6ea; }
QTabBar::tab:disabled { color: #9ca3af; background: #f3f4f6; }

QLabel#title { font-size: 22px; font-weight: 600; color: #111827; padding: 4px 0 12px 0; }
QLabel#subtitle { font-size: 12px; color: #6b7280; padding-bottom: 16px; }
QLabel#balance { font-size: 32px; font-weight: 700; color: #2563eb; padding: 8px 0; }
QLabel#status { color: #6b7280; font-size: 12px; padding: 4px 0; }
QLabel#status[err="true"] { color: #dc2626; }
QLabel#status[ok="true"]  { color: #059669; }

QLineEdit {
    padding: 8px 12px;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    background: #ffffff;
    selection-background-color: #2563eb;
}
QLineEdit:focus { border: 1px solid #2563eb; }

QPushButton {
    background: #2563eb;
    color: #ffffff;
    border: none;
    border-radius: 6px;
    padding: 8px 20px;
    font-weight: 500;
}
QPushButton:hover { background: #1d4ed8; }
QPushButton:pressed { background: #1e40af; }
QPushButton:disabled { background: #9ca3af; }
QPushButton[variant="secondary"] {
    background: #ffffff;
    color: #2563eb;
    border: 1px solid #2563eb;
}
QPushButton[variant="secondary"]:hover { background: #eff6ff; }

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
QTableWidget::item { padding: 6px 8px; }
QTableWidget::item:selected { background: #dbeafe; color: #1e40af; }

QWidget#mapOverlay { background: rgba(255,255,255,0.9); border-top: 1px solid #e5e7eb; }
)";

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), client_("124.220.92.243", 8888) {
    setStyleSheet(QString::fromUtf8(kStyle));

    stack_ = new QStackedWidget(this);
    login_     = new LoginView(&client_, &session_, stack_);
    stack_->addWidget(login_);
    setCentralWidget(stack_);

    connect(login_, &LoginView::logged_in, this, &MainWindow::on_logged_in);

    setWindowTitle(QString::fromUtf8("共享单车 · 用户客户端"));
    resize(960, 640);
}

void MainWindow::enter_main_ui() {
    if (tabs_) return;

    tabs_       = new QTabWidget(stack_);
    map_view_   = new MapView(&client_, &session_, tabs_);
    ride_view_  = new RideView(&client_, &session_, tabs_);
    wallet_     = new WalletView(&client_, &session_, tabs_);
    records_    = new RecordsView(&client_, &session_, tabs_);
    history_    = new RideHistoryView(&client_, &session_, tabs_);

    tabs_->addTab(map_view_,  QString::fromUtf8("地图"));
    tabs_->addTab(ride_view_, QString::fromUtf8("骑行中"));
    tabs_->addTab(wallet_,    QString::fromUtf8("钱包"));
    tabs_->addTab(records_,   QString::fromUtf8("账单"));
    tabs_->addTab(history_,   QString::fromUtf8("历史"));

    tabs_->setTabEnabled(1, false);

    stack_->addWidget(tabs_);
    stack_->setCurrentWidget(tabs_);

    connect(map_view_, &MapView::unlockRequested,
            this, &MainWindow::on_unlock_requested);
    connect(ride_view_, &RideView::ended,
            this, &MainWindow::on_ride_ended);
    connect(history_, &RideHistoryView::viewDetailRequested,
            this, &MainWindow::on_view_detail);

    wallet_->refresh_balance();
    records_->refresh();
    history_->refresh();

    check_orphan_ride();
}

void MainWindow::on_logged_in() {
    enter_main_ui();
}

void MainWindow::on_unlock_requested(const QString& bike_no) {
    if (!session_.logged_in()) return;
    double lat = map_view_->my_lat();
    double lng = map_view_->my_lng();

    map_view_->set_status(QString::fromUtf8("正在解锁 %1…").arg(bike_no));
    QtConcurrent::run([this, bike_no, lat, lng] {
        try {
            auto rsp = client_.scan_unlock(session_.token, bike_no.toStdString(), lat, lng);
            QMetaObject::invokeMethod(this, [this, rsp, bike_no, lat, lng] {
                if (rsp.code() != 200) {
                    QMessageBox::warning(this, QString::fromUtf8("解锁失败"),
                        QString::fromUtf8("解锁失败:%1").arg(QString::fromStdString(rsp.desc())));
                    return;
                }
                QString ride_no = QString::fromStdString(rsp.ride_no());
                active_ride_ = ride_no;
                QSettings().setValue("active_ride", ride_no);

                std::uint32_t seed = static_cast<std::uint32_t>(QDateTime::currentSecsSinceEpoch());
                ride_view_->start_ride(ride_no, lat, lng, seed);
                tabs_->setTabEnabled(1, true);
                tabs_->setCurrentIndex(1);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                QMessageBox::critical(this, QString::fromUtf8("网络异常"),
                    QString::fromUtf8("解锁异常:%1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

void MainWindow::on_ride_ended(const QString&, int amount_cent, int) {
    active_ride_.clear();
    QSettings().remove("active_ride");

    tabs_->setTabEnabled(1, false);
    tabs_->setCurrentIndex(3);
    records_->refresh();
    wallet_->refresh_balance();

    QMessageBox::information(this, QString::fromUtf8("骑行结束"),
        QString::fromUtf8("本次消费 ¥ %L1").arg(amount_cent / 100.0, 0, 'f', 2));
}

void MainWindow::on_view_detail(const QString& ride_no) {
    auto* dlg = new RideDetailDialog(&client_, QString::fromStdString(session_.token),
                                     ride_no, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModal(true);
    dlg->show();
}

void MainWindow::check_orphan_ride() {
    QString ride_no = QSettings().value("active_ride").toString();
    if (ride_no.isEmpty()) return;

    auto ret = QMessageBox::question(this,
        QString::fromUtf8("检测到未结订单"),
        QString::fromUtf8("订单 %1 上次未结束,是否尝试结束?\n(若服务端已重启,此订单已失效)").arg(ride_no),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        QSettings().remove("active_ride");
        return;
    }

    QtConcurrent::run([this, ride_no] {
        try {
            auto rsp = client_.end_ride(session_.token, ride_no.toStdString(),
                                        39.9821, 116.3145);
            QMetaObject::invokeMethod(this, [this, rsp] {
                QSettings().remove("active_ride");
                QMessageBox::information(this, QString::fromUtf8("已结算"),
                    QString::fromUtf8("订单已结束,消费 ¥ %L1").arg(rsp.amount_cent() / 100.0, 0, 'f', 2));
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                QSettings().remove("active_ride");
                QMessageBox::warning(this, QString::fromUtf8("订单已失效"),
                    QString::fromUtf8("无法结算:%1\n已清理本地状态。").arg(QString::fromStdString(msg)));
            });
        }
    });
}

} // namespace bike::client
