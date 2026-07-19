#include "ride_history_view.hpp"

#include <QColor>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

RideHistoryView::RideHistoryView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    auto* title = new QLabel(QString::fromUtf8("历史骑行"), this);
    title->setObjectName("title");
    v->addWidget(title);
    auto* sub = new QLabel(QString::fromUtf8("双击任一行查看完整轨迹"), this);
    sub->setObjectName("subtitle");
    v->addWidget(sub);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({
        QString::fromUtf8("订单号"),
        QString::fromUtf8("起始时间"),
        QString::fromUtf8("距离 / 时长"),
        QString::fromUtf8("金额"),
    });
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->setMinimumHeight(320);
    v->addWidget(table_);

    auto* h = new QHBoxLayout;
    btn_refresh_ = new QPushButton(QString::fromUtf8("刷新"), this);
    btn_refresh_->setProperty("variant", "secondary");
    h->addWidget(btn_refresh_);
    h->addStretch();
    v->addLayout(h);

    lb_status_ = new QLabel(this);
    lb_status_->setObjectName("status");
    v->addWidget(lb_status_);

    connect(btn_refresh_, &QPushButton::clicked, this, &RideHistoryView::refresh);
    connect(table_, &QTableWidget::cellDoubleClicked,
            this, &RideHistoryView::on_row_double_clicked);
}

void RideHistoryView::refresh() {
    if (!session_->logged_in()) {
        lb_status_->setText(QString::fromUtf8("尚未登录"));
        return;
    }
    lb_status_->setText(QString::fromUtf8("正在加载历史…"));
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->list_rides(session_->token, 50);
            QMetaObject::invokeMethod(this, [this, rsp] {
                table_->setRowCount(rsp.rides_size());
                for (int i = 0; i < rsp.rides_size(); ++i) {
                    const auto& r = rsp.rides(i);
                    auto* it_no = new QTableWidgetItem(QString::fromStdString(r.ride_no()));
                    auto* it_tm = new QTableWidgetItem(QString::fromStdString(r.start_tm()));
                    auto* it_ds = new QTableWidgetItem(QString::fromUtf8("%1 km / %2 min")
                        .arg(r.distance_m() / 1000.0, 0, 'f', 2)
                        .arg((r.duration_sec() + 30) / 60));
                    auto* it_amt = new QTableWidgetItem(QString("¥ %L1")
                        .arg(r.amount_cent() / 100.0, 0, 'f', 2));
                    it_amt->setForeground(QColor("#dc2626"));
                    table_->setItem(i, 0, it_no);
                    table_->setItem(i, 1, it_tm);
                    table_->setItem(i, 2, it_ds);
                    table_->setItem(i, 3, it_amt);
                }
                lb_status_->setText(QString::fromUtf8("共 %1 条").arg(rsp.rides_size()));
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString::fromUtf8("加载失败:%1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

void RideHistoryView::on_row_double_clicked(int row) {
    auto* it = table_->item(row, 0);
    if (!it) return;
    emit viewDetailRequested(it->text());
}

} // namespace bike::client
