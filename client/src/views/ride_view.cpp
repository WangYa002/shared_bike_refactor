#include "ride_view.hpp"

#include <chrono>
#include <QChar>
#include <QHBoxLayout>
#include <QString>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

namespace {
QString format_hms(int sec) {
    int h = sec / 3600;
    int m = (sec / 60) % 60;
    int s = sec % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

int estimate_fee_cent(int sec) {
    constexpr int base = 15 * 60;
    constexpr int step = 15 * 60;
    if (sec <= base) return 100;
    int extra = sec - base;
    int chunks = (extra + step - 1) / step;
    return 100 + chunks * 50;
}
} // namespace

RideView::RideView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session), sim_(0) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    lb_title_ = new QLabel(QString::fromUtf8("尚未开始骑行"), this);
    lb_title_->setObjectName("title");
    v->addWidget(lb_title_);

    lb_timer_ = new QLabel("00:00:00", this);
    lb_timer_->setObjectName("balance");
    v->addWidget(lb_timer_);

    lb_distance_ = new QLabel(QString::fromUtf8("📏 0.00 km    💰 约 ¥ 1.00"), this);
    lb_distance_->setObjectName("subtitle");
    v->addWidget(lb_distance_);

    lb_estimate_ = new QLabel(this);
    lb_estimate_->setObjectName("status");
    v->addWidget(lb_estimate_);

    v->addStretch();

    btn_end_ = new QPushButton(QString::fromUtf8("结束骑行"), this);
    btn_end_->setMinimumHeight(44);
    btn_end_->setStyleSheet("background:#dc2626;");
    btn_end_->setEnabled(false);
    v->addWidget(btn_end_);

    lb_status_ = new QLabel(this);
    lb_status_->setObjectName("status");
    v->addWidget(lb_status_);

    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &RideView::on_tick);
    connect(btn_end_, &QPushButton::clicked, this, &RideView::on_end_clicked);
}

void RideView::start_ride(const QString& ride_no, double start_lat, double start_lng,
                          std::uint32_t sim_seed) {
    ride_no_   = ride_no;
    start_lat_ = start_lat;
    start_lng_ = start_lng;
    elapsed_sec_ = 0;
    active_ = true;
    sim_ = TrajectorySim(sim_seed);
    sim_.start(start_lat, start_lng);

    lb_title_->setText(QString::fromUtf8("骑行中  %1").arg(ride_no));
    lb_timer_->setText("00:00:00");
    lb_distance_->setText(QString::fromUtf8("📏 0.00 km    💰 约 ¥ 1.00"));
    btn_end_->setEnabled(true);
    lb_status_->setText(QString());

    timer_.start();
}

void RideView::on_tick() {
    if (!active_) return;
    ++elapsed_sec_;
    auto p = sim_.step();
    emit positionUpdated(p.x(), p.y());

    client_->report_position(ride_no_.toStdString(), sim_.seq(),
                             p.x(), p.y(), elapsed_sec_);

    lb_timer_->setText(format_hms(elapsed_sec_));
    double km = sim_.distance_m() / 1000.0;
    int fee = estimate_fee_cent(elapsed_sec_);
    lb_distance_->setText(QString::fromUtf8("📏 %L1 km    💰 约 ¥ %L2")
                              .arg(km, 0, 'f', 2)
                              .arg(fee / 100.0, 0, 'f', 2));
}

void RideView::on_end_clicked() {
    if (!active_) return;
    btn_end_->setEnabled(false);
    lb_status_->setText(QString::fromUtf8("正在结束骑行…"));
    timer_.stop();

    double end_lat = sim_.current().x();
    double end_lng = sim_.current().y();
    QString ride_no = ride_no_;

    QtConcurrent::run([this, ride_no, end_lat, end_lng] {
        try {
            auto rsp = client_->end_ride(session_->token, ride_no.toStdString(),
                                         end_lat, end_lng);
            QMetaObject::invokeMethod(this, [this, rsp] {
                active_ = false;
                lb_title_->setText(QString::fromUtf8("骑行已结束"));
                lb_status_->setText(QString::fromUtf8("消费 ¥ %L1,余额 ¥ %L2")
                                        .arg(rsp.amount_cent() / 100.0, 0, 'f', 2)
                                        .arg(rsp.balance_after() / 100.0, 0, 'f', 2));
                emit ended(QString::fromStdString(rsp.desc().empty() ? "" : rsp.desc()),
                           rsp.amount_cent(), rsp.balance_after());
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString::fromUtf8("结束失败:%1,请重试").arg(QString::fromStdString(msg)));
                btn_end_->setEnabled(true);
                timer_.start();
            });
        }
    });
}

} // namespace bike::client
