#include "records_view.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

RecordsView::RecordsView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    auto* title = new QLabel(QString::fromUtf8("账单流水"), this);
    title->setObjectName("title");
    v->addWidget(title);

    auto* sub = new QLabel(QString::fromUtf8("最近的账户变动记录"), this);
    sub->setObjectName("subtitle");
    v->addWidget(sub);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({
        QString::fromUtf8("类型"),
        QString::fromUtf8("金额"),
        QString::fromUtf8("时间"),
    });
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->setMinimumHeight(280);
    v->addWidget(table_);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    btn_refresh_ = new QPushButton(QString::fromUtf8("刷新流水"), this);
    btn_refresh_->setProperty("variant", "secondary");
    btn_refresh_->setMinimumHeight(32);
    btn_refresh_->setCursor(Qt::PointingHandCursor);
    actions->addWidget(btn_refresh_);
    actions->addStretch();
    v->addLayout(actions);

    lb_status_ = new QLabel(QString::fromUtf8("点击刷新查看记录"), this);
    lb_status_->setObjectName("status");
    v->addWidget(lb_status_);

    connect(btn_refresh_, &QPushButton::clicked, this, &RecordsView::refresh);
}

void RecordsView::refresh() {
    if (!session_->logged_in()) {
        set_status(QString::fromUtf8("尚未登录"), true);
        return;
    }
    set_status(QString::fromUtf8("正在加载…"));
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->list_records(session_->token);
            QMetaObject::invokeMethod(this, [this, rsp] {
                table_->setRowCount(rsp.records_size());
                for (int i = 0; i < rsp.records_size(); ++i) {
                    const auto& r = rsp.records(i);
                    QString type = (r.type() == 1)
                        ? QString::fromUtf8("充值")
                        : QString::fromUtf8("消费");
                    double yuan = r.amount() / 100.0;
                    QString amt = QString("%L1 元").arg(yuan, 0, 'f', 2);
                    if (r.type() == 1) amt = "+ " + amt;
                    QString tm = QDateTime::fromSecsSinceEpoch(
                        static_cast<qint64>(r.timestamp())).toString("yyyy-MM-dd HH:mm:ss");

                    auto* it_type = new QTableWidgetItem(type);
                    it_type->setTextAlignment(Qt::AlignCenter);

                    auto* it_amt = new QTableWidgetItem(amt);
                    it_amt->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    if (r.type() == 1) {
                        it_amt->setForeground(QColor("#059669"));
                    } else {
                        it_amt->setForeground(QColor("#dc2626"));
                    }

                    auto* it_time = new QTableWidgetItem(tm);
                    it_time->setTextAlignment(Qt::AlignCenter);
                    it_time->setForeground(QColor("#6b7280"));

                    table_->setItem(i, 0, it_type);
                    table_->setItem(i, 1, it_amt);
                    table_->setItem(i, 2, it_time);
                }
                set_status(QString::fromUtf8("共 %1 条记录").arg(rsp.records_size()), false, true);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                set_status(QString::fromUtf8("加载失败:%1").arg(QString::fromStdString(msg)), true);
            });
        }
    });
}

void RecordsView::set_status(const QString& text, bool is_err, bool is_ok) {
    lb_status_->setText(text);
    lb_status_->setProperty("err", is_err);
    lb_status_->setProperty("ok",  is_ok);
    lb_status_->style()->unpolish(lb_status_);
    lb_status_->style()->polish(lb_status_);
}

} // namespace bike::client
