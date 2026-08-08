#include "net/api_bridge.hpp"

#include "net/tcp_worker.hpp"

#include <bike.pb.h>
#include <bike/protocol.hpp>

#include <QLoggingCategory>
#include <QMetaObject>
#include <QSettings>
#include <QThread>

#include <cstdint>

namespace bike::client {

Q_DECLARE_LOGGING_CATEGORY(lcNet)  // 定义在 tcp_worker.cpp

namespace {

constexpr quint16 eid(bike::Event e) { return bike::event_id(e); }
constexpr const char* kActiveRideKey = "active_ride";

bool isKnownResponse(quint16 rsp) {
    switch (rsp) {
        case 0x02: case 0x04: case 0x06: case 0x08: case 0x10: case 0x12:
        case 0x14: case 0x18: case 0x1A: case 0x1C: case 0x1E:
            return true;
        default:
            return false;
    }
}

// 响应 eid -> 对应请求 eid。特例: 账单响应 0x10 -> 请求 0x09; 其余响应 = 请求 + 1。
quint16 rspToReq(quint16 rsp) {
    if (rsp == eid(bike::Event::ListAccountRecordsResponse))
        return eid(bike::Event::ListAccountRecordsRequest);
    return static_cast<quint16>(rsp - 1);
}

template <typename Rsp>
bool parse_pb(const QByteArray& payload, Rsp& m) {
    return m.ParseFromArray(payload.constData(), static_cast<int>(payload.size()));
}

} // namespace

ApiBridge::ApiBridge(const QString& host, quint16 port, QObject* parent)
    : QObject(parent), host_(host), port_(port) {
    // 装配专属网络线程: worker 无 parent 才能 moveToThread。
    thread_ = new QThread(this);
    worker_ = new TcpWorker;
    worker_->moveToThread(thread_);

    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    // 线程启动即发起连接 (lambda 以 worker_ 为 context, 投递到 worker 线程执行)。
    connect(thread_, &QThread::started, worker_,
            [this] { worker_->connectToServer(host_, port_); });

    // 跨线程自动 QueuedConnection: GUI 线程 <-> worker 线程, 零锁。
    connect(this, &ApiBridge::requestSend, worker_, &TcpWorker::sendFrame);
    connect(worker_, &TcpWorker::frameReady, this, &ApiBridge::onFrameReady);
    connect(worker_, &TcpWorker::connected, this, &ApiBridge::onWorkerConnected);
    connect(worker_, &TcpWorker::disconnected, this, &ApiBridge::onWorkerDisconnected);
    connect(worker_, &TcpWorker::errorOccurred, this,
            [](const QString& msg) { qCWarning(lcNet) << "worker error:" << msg; });

    thread_->start();
}

ApiBridge::~ApiBridge() {
    if (worker_) {
        TcpWorker* w = worker_;
        QMetaObject::invokeMethod(w, [w] { w->shutdown(); }, Qt::QueuedConnection);
    }
    if (thread_) {
        thread_->quit();
        if (!thread_->wait(2000)) {
            // #7: 防御性处理 —— 正常情况下 worker shutdown 后事件循环会立刻退出。
            // 走到这里说明 worker 线程卡死; terminate 有资源未清理/socket 泄漏风险,
            // 但优于带着仍在运行的 QThread 析构(未 wait 的运行中线程析构是致命错误)。
            qCWarning(lcNet) << "worker thread did not exit in 2s, terminating";
            thread_->terminate();
            thread_->wait(500);
        }
    }
}

quint32 ApiBridge::nextSeq() {
    std::uint32_t s = ++seq_counter_;
    if (s == 0) s = ++seq_counter_;  // 回绕跳过 0
    return s;
}

bool ApiBridge::beginRequest(quint16 req_eid, const char* what) {
    if (in_flight_.contains(req_eid)) {
        qCWarning(lcNet) << "request rejected, previous still in flight:" << what;
        return false;
    }
    return true;
}

template <typename Req>
void ApiBridge::sendRequest(std::uint16_t req_eid, const Req& req, const char* what) {
    const quint32 seq = nextSeq();
    in_flight_.insert(req_eid, seq);
    const std::string payload = req.SerializeAsString();
    emit requestSend(req_eid, seq,
                     QByteArray(payload.data(), static_cast<int>(payload.size())));
    qCDebug(lcNet) << "sent" << what << "seq" << seq;
}

// ===================== 12 个异步入口 =====================

void ApiBridge::sendMobileCode(const QString& mobile) {
    if (!beginRequest(eid(bike::Event::MobileRequest), "sendMobileCode")) {
        emit mobileCodeResult(false, 0, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::mobile_request req;
    req.set_mobile(mobile.toStdString());
    sendRequest(eid(bike::Event::MobileRequest), req, "sendMobileCode");
}

void ApiBridge::login(const QString& mobile, int icode) {
    if (!beginRequest(eid(bike::Event::LoginRequest), "login")) {
        emit loginResult(false, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    pending_mobile_ = mobile;
    tutorial::login_request req;
    req.set_mobile(mobile.toStdString());
    req.set_icode(icode);
    sendRequest(eid(bike::Event::LoginRequest), req, "login");
}

void ApiBridge::recharge(int amount_cent) {
    if (!beginRequest(eid(bike::Event::RechargeRequest), "recharge")) {
        emit balanceReady(false, 0, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::recharge_request req;
    req.set_session_token(token_);
    req.set_amount(amount_cent);
    sendRequest(eid(bike::Event::RechargeRequest), req, "recharge");
}

void ApiBridge::refreshBalance() {
    if (!beginRequest(eid(bike::Event::AccountBalanceRequest), "refreshBalance")) {
        emit balanceReady(false, 0, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::account_balance_request req;
    req.set_session_token(token_);
    sendRequest(eid(bike::Event::AccountBalanceRequest), req, "refreshBalance");
}

void ApiBridge::listRecords() {
    if (!beginRequest(eid(bike::Event::ListAccountRecordsRequest), "listRecords")) {
        emit recordsReady(false, {}, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::list_account_records_request req;
    req.set_session_token(token_);
    sendRequest(eid(bike::Event::ListAccountRecordsRequest), req, "listRecords");
}

void ApiBridge::refreshNearbyBikes(double lat, double lng) {
    if (!beginRequest(eid(bike::Event::ListNearbyBikesRequest), "refreshNearbyBikes")) {
        emit nearbyBikesReady(false, {}, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::list_nearby_bikes_request req;
    req.set_session_token(token_);
    req.set_lat(lat);
    req.set_lng(lng);
    req.set_radius_m(1000.0);  // 与旧 MapView 一致: 1km 半径
    sendRequest(eid(bike::Event::ListNearbyBikesRequest), req, "refreshNearbyBikes");
}

void ApiBridge::scanUnlock(const QString& bike_no, double lat, double lng) {
    if (!beginRequest(eid(bike::Event::ScanUnlockRequest), "scanUnlock")) {
        emit unlockResult(false, QString(), QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::scan_unlock_request req;
    req.set_session_token(token_);
    req.set_bike_no(bike_no.toStdString());
    req.set_lat(lat);
    req.set_lng(lng);
    sendRequest(eid(bike::Event::ScanUnlockRequest), req, "scanUnlock");
}

void ApiBridge::reportPosition(const QString& ride_no, int seq_i,
                               double lat, double lng, int elapsed_sec) {
    // fire-and-forget: 单向事件 0x15, 不占单飞槽位, 不等响应。
    // 替代旧实现里"分离线程 + 独立 socket + atomic 背压"的方案。
    tutorial::ride_position_report req;
    req.set_ride_no(ride_no.toStdString());
    req.set_seq(seq_i);
    req.set_lat(lat);
    req.set_lng(lng);
    req.set_elapsed_sec(elapsed_sec);
    const std::string payload = req.SerializeAsString();
    emit requestSend(eid(bike::Event::RidePositionReport), nextSeq(),
                     QByteArray(payload.data(), static_cast<int>(payload.size())));
}

void ApiBridge::endRide(double lat, double lng) {
    if (active_ride_.isEmpty()) {
        emit rideEnded(false, 0, 0, QStringLiteral("当前没有进行中的订单"));
        return;
    }
    if (!beginRequest(eid(bike::Event::EndRideRequest), "endRide")) {
        emit rideEnded(false, 0, 0, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::end_ride_request req;
    req.set_session_token(token_);
    req.set_ride_no(active_ride_.toStdString());
    req.set_end_lat(lat);
    req.set_end_lng(lng);
    sendRequest(eid(bike::Event::EndRideRequest), req, "endRide");
}

void ApiBridge::reportDamage(const QString& bike_no, const QString& note) {
    if (!beginRequest(eid(bike::Event::ReportDamageRequest), "reportDamage")) {
        emit damageResult(false, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::report_damage_request req;
    req.set_session_token(token_);
    req.set_bike_no(bike_no.toStdString());
    req.set_note(note.toStdString());
    sendRequest(eid(bike::Event::ReportDamageRequest), req, "reportDamage");
}

void ApiBridge::rideDetail(const QString& ride_no) {
    if (!beginRequest(eid(bike::Event::GetRideDetailRequest), "rideDetail")) {
        emit rideDetailReady(false, {}, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::get_ride_detail_request req;
    req.set_session_token(token_);
    req.set_ride_no(ride_no.toStdString());
    sendRequest(eid(bike::Event::GetRideDetailRequest), req, "rideDetail");
}

void ApiBridge::listRides(int limit) {
    if (!beginRequest(eid(bike::Event::ListRidesRequest), "listRides")) {
        emit ridesReady(false, {}, QStringLiteral("上一个请求仍在处理"));
        return;
    }
    tutorial::list_rides_request req;
    req.set_session_token(token_);
    req.set_limit(limit);
    sendRequest(eid(bike::Event::ListRidesRequest), req, "listRides");
}

void ApiBridge::clearActiveRide() {
    active_ride_.clear();
    QSettings().remove(kActiveRideKey);
    emit sessionChanged();
}

// ===================== 网络状态 =====================

void ApiBridge::onWorkerConnected() {
    seq_counter_ = 0;   // seq 每连接自增
    // 旧连接上的在途请求不可能再有响应: 发失败回执后清空(不静默丢弃)。
    failInFlight(QStringLiteral("连接已重建, 请重试"));
    link_up_ = true;
    qCInfo(lcNet) << "link up";
    emit linkStateChanged();
}

void ApiBridge::onWorkerDisconnected(const QString& reason) {
    // #4: 断线时对在途请求逐个发失败回执, 让 QML 复位 pending 标志
    // (否则 pendingLogin/pending 等永久卡死, 按钮不可再点)。
    failInFlight(QStringLiteral("连接中断: ") + reason);
    link_up_ = false;
    qCInfo(lcNet) << "link down:" << reason;
    emit linkStateChanged();
}

void ApiBridge::failInFlight(const QString& msg) {
    for (auto it = in_flight_.cbegin(); it != in_flight_.cend(); ++it) {
        switch (it.key()) {
            case 0x01: emit mobileCodeResult(false, 0, msg); break;
            case 0x03: emit loginResult(false, msg); break;
            case 0x05:
            case 0x07: emit balanceReady(false, 0, msg); break;
            case 0x09: emit recordsReady(false, {}, msg); break;
            case 0x11: emit nearbyBikesReady(false, {}, msg); break;
            case 0x13: emit unlockResult(false, QString(), msg); break;
            case 0x17: emit rideEnded(false, 0, 0, msg); break;
            case 0x19: emit damageResult(false, msg); break;
            case 0x1B: emit rideDetailReady(false, {}, msg); break;
            case 0x1D: emit ridesReady(false, {}, msg); break;
            default: break;
        }
    }
    in_flight_.clear();
}

// ===================== 响应解码 (GUI 线程) =====================

void ApiBridge::onFrameReady(quint16 rsp_eid, quint32 seq, const QByteArray& payload) {
    if (!isKnownResponse(rsp_eid)) {
        qCWarning(lcNet) << "unknown response eid" << Qt::hex << rsp_eid;
        return;
    }
    const quint16 req_eid = rspToReq(rsp_eid);
    auto it = in_flight_.find(req_eid);
    if (it == in_flight_.end()) {
        qCDebug(lcNet) << "unsolicited response eid" << Qt::hex << rsp_eid;
        return;
    }
    if (it.value() != seq) {
        qCWarning(lcNet) << "response seq mismatch eid" << Qt::hex << rsp_eid
                         << "expect" << it.value() << "got" << seq;
        return;
    }
    in_flight_.erase(it);
    handleFrame(rsp_eid, payload);
}

void ApiBridge::handleFrame(quint16 rsp_eid, const QByteArray& payload) {
    switch (rsp_eid) {
        case 0x02: {  // 验证码 (旧版语义: 测试环境直接回显 icode)
            tutorial::mobile_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit mobileCodeResult(false, 0, QStringLiteral("响应解析失败"));
                return;
            }
            emit mobileCodeResult(true, rsp.icode(), QString::fromStdString(rsp.data()));
            return;
        }
        case 0x04: {  // 登录
            tutorial::login_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit loginResult(false, QStringLiteral("响应解析失败"));
                return;
            }
            const bool ok = rsp.code() == 200 && !rsp.session_token().empty();
            if (ok) {
                token_ = rsp.session_token();
                mobile_ = pending_mobile_;
                // 孤儿订单恢复: 与旧 MainWindow::check_orphan_ride 同源。
                active_ride_ = QSettings().value(kActiveRideKey).toString();
                emit sessionChanged();
            }
            emit loginResult(ok, QString::fromStdString(rsp.desc()));
            return;
        }
        case 0x06: {  // 充值
            tutorial::recharge_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit balanceReady(false, 0, QStringLiteral("响应解析失败"));
                return;
            }
            emit balanceReady(rsp.code() == 200, rsp.balance(),
                              rsp.code() == 200 ? QStringLiteral("充值成功")
                                                : QString::fromStdString(rsp.desc()));
            return;
        }
        case 0x08: {  // 余额
            tutorial::account_balance_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit balanceReady(false, 0, QStringLiteral("响应解析失败"));
                return;
            }
            emit balanceReady(rsp.code() == 200, rsp.balance(), QString());
            return;
        }
        case 0x10: {  // 账单 (特例: 0x09 -> 0x10)
            tutorial::list_account_records_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit recordsReady(false, {}, QStringLiteral("响应解析失败"));
                return;
            }
            QVariantList rows;
            rows.reserve(rsp.records_size());
            for (int i = 0; i < rsp.records_size(); ++i) {
                const auto& r = rsp.records(i);
                rows.append(QVariantMap{
                    {"type", r.type()},
                    {"amount", r.amount()},
                    {"timestamp", static_cast<qlonglong>(r.timestamp())},
                });
            }
            emit recordsReady(rsp.code() == 200, rows, QString());
            return;
        }
        case 0x12: {  // 附近车辆
            tutorial::list_nearby_bikes_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit nearbyBikesReady(false, {}, QStringLiteral("响应解析失败"));
                return;
            }
            QVariantList bikes;
            bikes.reserve(rsp.bikes_size());
            for (int i = 0; i < rsp.bikes_size(); ++i) {
                const auto& b = rsp.bikes(i);
                bikes.append(QVariantMap{
                    {"bike_no", QString::fromStdString(b.bike_no())},
                    {"lat", b.lat()},
                    {"lng", b.lng()},
                    {"status", b.status()},
                });
            }
            emit nearbyBikesReady(rsp.code() == 200, bikes, QString());
            return;
        }
        case 0x14: {  // 扫码解锁
            tutorial::scan_unlock_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit unlockResult(false, QString(), QStringLiteral("响应解析失败"));
                return;
            }
            const bool ok = rsp.code() == 200;
            QString ride_no;
            if (ok) {
                ride_no = QString::fromStdString(rsp.ride_no());
                active_ride_ = ride_no;
                QSettings().setValue(kActiveRideKey, ride_no);
                emit sessionChanged();
            }
            emit unlockResult(ok, ride_no, QString::fromStdString(rsp.desc()));
            return;
        }
        case 0x18: {  // 结束骑行
            tutorial::end_ride_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit rideEnded(false, 0, 0, QStringLiteral("响应解析失败"));
                return;
            }
            const bool ok = rsp.code() == 200;
            if (ok) {
                active_ride_.clear();
                QSettings().remove(kActiveRideKey);
                emit sessionChanged();
            }
            emit rideEnded(ok, rsp.amount_cent(), rsp.balance_after(),
                           QString::fromStdString(rsp.desc()));
            return;
        }
        case 0x1A: {  // 报修
            tutorial::report_damage_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit damageResult(false, QStringLiteral("响应解析失败"));
                return;
            }
            emit damageResult(rsp.code() == 200, QString::fromStdString(rsp.desc()));
            return;
        }
        case 0x1C: {  // 订单详情
            tutorial::get_ride_detail_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit rideDetailReady(false, {}, QStringLiteral("响应解析失败"));
                return;
            }
            QVariantList pts;
            pts.reserve(rsp.points_size());
            for (int i = 0; i < rsp.points_size(); ++i) {
                const auto& p = rsp.points(i);
                pts.append(QVariantMap{
                    {"lat", p.lat()},
                    {"lng", p.lng()},
                    {"elapsed_sec", p.elapsed_sec()},
                });
            }
            emit rideDetailReady(rsp.code() == 200, QVariantMap{
                {"ride_no", QString::fromStdString(rsp.ride_no())},
                {"duration_sec", rsp.duration_sec()},
                {"distance_m", rsp.distance_m()},
                {"amount_cent", rsp.amount_cent()},
                {"start_tm", QString::fromStdString(rsp.start_tm())},
                {"end_tm", QString::fromStdString(rsp.end_tm())},
                {"points", pts},
            }, QString());
            return;
        }
        case 0x1E: {  // 骑行历史
            tutorial::list_rides_response rsp;
            if (!parse_pb(payload, rsp)) {
                emit ridesReady(false, {}, QStringLiteral("响应解析失败"));
                return;
            }
            QVariantList rides;
            rides.reserve(rsp.rides_size());
            for (int i = 0; i < rsp.rides_size(); ++i) {
                const auto& r = rsp.rides(i);
                rides.append(QVariantMap{
                    {"ride_no", QString::fromStdString(r.ride_no())},
                    {"start_tm", QString::fromStdString(r.start_tm())},
                    {"duration_sec", r.duration_sec()},
                    {"distance_m", r.distance_m()},
                    {"amount_cent", r.amount_cent()},
                });
            }
            emit ridesReady(rsp.code() == 200, rides, QString());
            return;
        }
        default:
            qCWarning(lcNet) << "unhandled response eid" << Qt::hex << rsp_eid;
    }
}

} // namespace bike::client
