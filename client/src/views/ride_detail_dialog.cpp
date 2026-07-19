#include "ride_detail_dialog.hpp"

#include <QChar>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QWebChannel>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

namespace {
QString format_hms(int sec) {
    int h = sec / 3600;
    int m = (sec / 60) % 60;
    int s = sec % 60;
    return QString("%1:%2:%3").arg(h,2,10,QChar('0')).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
}
} // namespace

RideDetailDialog::RideDetailDialog(BackendClient* client, const QString& token,
                                   const QString& ride_no, QWidget* parent)
    : QDialog(parent), client_(client), token_(token), ride_no_(ride_no) {
    setWindowTitle(QString::fromUtf8("订单 %1").arg(ride_no));
    resize(720, 600);

    auto* v = new QVBoxLayout(this);

    lb_meta_ = new QLabel(QString::fromUtf8("正在加载…"), this);
    lb_meta_->setObjectName("subtitle");
    v->addWidget(lb_meta_);

    web_ = new QWebEngineView(this);
    auto* channel = new QWebChannel(web_->page());
    bridge_ = new MapBridge(this);
    channel->registerObject(QStringLiteral("bridge"), bridge_);
    web_->page()->setWebChannel(channel);
    web_->load(QUrl("qrc:/map.html"));
    v->addWidget(web_, 1);

    auto* h = new QHBoxLayout;
    btn_play_ = new QPushButton(QString::fromUtf8("▶ 回放"), this);
    btn_play_->setEnabled(false);
    auto* btn_close = new QPushButton(QString::fromUtf8("关闭"), this);
    btn_close->setProperty("variant", "secondary");
    lb_progress_ = new QLabel("00:00 / 00:00", this);
    lb_progress_->setObjectName("status");
    h->addWidget(btn_play_);
    h->addWidget(lb_progress_);
    h->addStretch();
    h->addWidget(btn_close);
    v->addLayout(h);

    connect(btn_close, &QPushButton::clicked, this, &QDialog::reject);
    connect(btn_play_, &QPushButton::clicked, this, &RideDetailDialog::on_play_pause);
    connect(&timer_, &QTimer::timeout, this, &RideDetailDialog::on_replay_tick);
    timer_.setInterval(50);

    QtConcurrent::run([this] {
        try {
            auto rsp = client_->get_ride_detail(token_.toStdString(), ride_no_.toStdString());
            QMetaObject::invokeMethod(this, [this, rsp] {
                detail_ = rsp;
                on_detail_loaded();
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_meta_->setText(QString::fromUtf8("加载失败:%1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

void RideDetailDialog::on_detail_loaded() {
    if (detail_.points_size() == 0) {
        lb_meta_->setText(QString::fromUtf8("此订单无轨迹数据"));
        return;
    }
    QJsonArray arr;
    for (int i = 0; i < detail_.points_size(); ++i) {
        const auto& p = detail_.points(i);
        QJsonObject o;
        o["lat"] = p.lat();
        o["lng"] = p.lng();
        o["elapsed_sec"] = p.elapsed_sec();
        arr.append(o);
    }
    QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

    int dur = detail_.duration_sec();
    double km = detail_.distance_m() / 1000.0;
    double yuan = detail_.amount_cent() / 100.0;
    lb_meta_->setText(QString::fromUtf8("时长 %1   距离 %L2 km   费用 ¥ %L3")
                          .arg(format_hms(dur))
                          .arg(km, 0, 'f', 2)
                          .arg(yuan, 0, 'f', 2));

    QMetaObject::invokeMethod(this, [this, json] {
        web_->page()->runJavaScript(QString("drawFullTrajectory(%1);").arg(json));
        lb_progress_->setText(QString("00:00 / %1").arg(format_hms(detail_.duration_sec())));
        btn_play_->setEnabled(true);
    }, Qt::QueuedConnection);
}

void RideDetailDialog::on_play_pause() {
    if (detail_.points_size() == 0) return;
    playing_ = !playing_;
    btn_play_->setText(playing_ ? QString::fromUtf8("⏸ 暂停") : QString::fromUtf8("▶ 回放"));
    if (playing_) {
        if (replay_idx_ >= detail_.points_size()) replay_idx_ = 0;
        timer_.start();
    } else {
        timer_.stop();
    }
}

void RideDetailDialog::on_replay_tick() {
    if (replay_idx_ >= detail_.points_size()) {
        timer_.stop();
        playing_ = false;
        btn_play_->setText(QString::fromUtf8("▶ 回放"));
        return;
    }
    const auto& p = detail_.points(replay_idx_);
    web_->page()->runJavaScript(
        QString("appendTrajectory(%1, %2);").arg(p.lat()).arg(p.lng()));
    lb_progress_->setText(QString("%1 / %2")
        .arg(format_hms(p.elapsed_sec()))
        .arg(format_hms(detail_.duration_sec())));
    ++replay_idx_;
}

} // namespace bike::client
