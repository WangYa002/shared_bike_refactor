#include "records_view.hpp"

#include <QDateTime>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

RecordsView::RecordsView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({"Type", "Amount (cents)", "Balance after", "Time"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(table_);

    btn_refresh_ = new QPushButton("Refresh", this);
    v->addWidget(btn_refresh_);

    connect(btn_refresh_, &QPushButton::clicked, this, &RecordsView::refresh);
}

void RecordsView::refresh() {
    if (!session_->logged_in()) return;
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->list_records(session_->token);
            QMetaObject::invokeMethod(this, [this, rsp] {
                table_->setRowCount(rsp.records_size());
                for (int i = 0; i < rsp.records_size(); ++i) {
                    const auto& r = rsp.records(i);
                    table_->setItem(i, 0, new QTableWidgetItem(
                        r.type() == 1 ? "Recharge" : "Consume"));
                    table_->setItem(i, 1, new QTableWidgetItem(QString::number(r.amount())));
                    table_->setItem(i, 2, new QTableWidgetItem("—"));
                    table_->setItem(i, 3, new QTableWidgetItem(
                        QDateTime::fromSecsSinceEpoch(static_cast<qint64>(r.timestamp()))
                            .toString(Qt::ISODate)));
                }
            });
        } catch (const std::exception&) {
            // silent on refresh
        }
    });
}

} // namespace bike::client
