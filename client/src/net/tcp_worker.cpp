#include "net/tcp_worker.hpp"

#include <bike/protocol.hpp>

#include <QLoggingCategory>
#include <QTcpSocket>
#include <QTimer>

#include <cstdint>

namespace bike::client {

Q_LOGGING_CATEGORY(lcNet, "bike.net")

namespace {
constexpr int kMaxPendingFrames = 128;      // 未连接期间最多缓存的待发帧数
constexpr int kBackoffInitMs = 500;
constexpr int kBackoffCapMs = 10000;
} // namespace

bool consume_frames(
    QByteArray& buf,
    const std::function<void(quint16 eid, quint32 seq, const QByteArray& payload)>& on_frame) {
    // 复用 common 的三态解码: NeedMore = 半包, BadFrame = magic/len 非法, Ok = 整帧。
    for (;;) {
        auto out = bike::decode_frame(
            reinterpret_cast<const std::uint8_t*>(buf.constData()),
            static_cast<std::size_t>(buf.size()));
        switch (out.status) {
            case bike::DecodeStatus::NeedMore:
                return true;
            case bike::DecodeStatus::BadFrame:
                return false;
            case bike::DecodeStatus::Ok: {
                QByteArray payload(out.frame.payload.data(),
                                   static_cast<int>(out.frame.payload.size()));
                buf.remove(0, static_cast<int>(out.consumed));
                if (on_frame) on_frame(out.frame.event_id, out.frame.seq, payload);
                break;
            }
        }
    }
}

TcpWorker::TcpWorker() = default;

void TcpWorker::ensureSocket() {
    if (sock_) return;
    sock_ = new QTcpSocket(this);  // 首次使用时创建于本对象所在线程
    connect(sock_, &QTcpSocket::connected, this, &TcpWorker::onConnected);
    connect(sock_, &QTcpSocket::disconnected, this, &TcpWorker::onDisconnected);
    connect(sock_, &QTcpSocket::readyRead, this, &TcpWorker::onReadyRead);
    // #5: 捕获裸 socket 指针而非 this->sock_, 避免 dropSocket 后取 errorString 的空指针窗口。
    connect(sock_, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                const QString msg = sock_ ? sock_->errorString() : QStringLiteral("socket error");
                emit errorOccurred(msg);
                // #2: 连接阶段失败(ConnectionRefused/HostNotFound 等)不会触发
                // disconnected, 若不补排程会永久失联; 与 onDisconnected 路径去重:
                // onDisconnected 只在 connecting_==true 时排程, 二者互斥。
                if (connecting_ && !shutting_down_ && sock_ &&
                    sock_->state() == QAbstractSocket::UnconnectedState) {
                    connecting_ = false;
                    scheduleReconnect();
                }
            });
}

void TcpWorker::connectToServer(const QString& host, quint16 port) {
    if (shutting_down_) return;
    host_ = host;
    port_ = port;
    if (connecting_ || (sock_ && sock_->state() != QAbstractSocket::UnconnectedState))
        return;
    ensureSocket();
    connecting_ = true;
    qCInfo(lcNet) << "connecting to" << host << port;
    sock_->connectToHost(host, port);
}

void TcpWorker::sendFrame(quint16 eid, quint32 seq, const QByteArray& payload) {
    bike::Frame f{.event_id = eid,
                  .seq = seq,
                  .payload = std::string(payload.constData(), payload.size())};
    std::vector<std::uint8_t> bytes;
    try {
        bytes = bike::encode(f);
    } catch (const std::exception& e) {
        emit errorOccurred(QStringLiteral("encode failed: %1").arg(e.what()));
        return;
    }
    QByteArray wire(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<int>(bytes.size()));

    if (sock_ && sock_->state() == QAbstractSocket::ConnectedState) {
        sock_->write(wire);
        return;
    }
    // 未连接: 仅缓存可丢帧(0x15 位置上报, fire-and-forget)等待重连后 flush;
    // 请求类帧直接丢弃不缓存 —— 上层 ApiBridge 已对断线在途请求发过失败回执,
    // 重连后 flush 旧请求帧会造成 unsolicited 响应。
    if (eid != bike::event_id(bike::Event::RidePositionReport)) return;
    if (pending_.size() >= kMaxPendingFrames) pending_.removeFirst();
    pending_.append(wire);
}

void TcpWorker::shutdown() {
    shutting_down_ = true;
    pending_.clear();
    dropSocket();
}

void TcpWorker::onConnected() {
    connecting_ = false;
    backoff_ms_ = kBackoffInitMs;
    rx_.clear();
    qCInfo(lcNet) << "connected";
    flushPending();
    emit connected();
}

void TcpWorker::onReadyRead() {
    rx_.append(sock_->readAll());
    bool ok = consume_frames(rx_, [this](quint16 eid, quint32 seq, const QByteArray& payload) {
        emit frameReady(eid, seq, payload);
    });
    if (!ok) {
        qCWarning(lcNet) << "bad frame from server, closing connection";
        emit errorOccurred(QStringLiteral("bad frame from server"));
        // #1: 坏 magic/len 后协议流已不可信, 整只丢弃 socket(deleteLater + 置空),
        // 而非仅剪断信号 —— 否则退避重连里 ensureSocket 见 sock_ 非空会直接
        // 复用旧 socket 且不重挂信号, 重连成功但 connected/readyRead 无人监听。
        dropSocket();
        onDisconnected();
        return;
    }
}

void TcpWorker::onDisconnected() {
    if (connecting_) connecting_ = false;
    rx_.clear();
    qCInfo(lcNet) << "disconnected";
    emit disconnected(QStringLiteral("connection closed"));
    // 正常连接后断开: 排程重连。连接阶段的失败已由 errorOccurred 处理路径
    // 排程(connecting_ 已复位), 此处不重复排程。
    scheduleReconnect();
}

void TcpWorker::dropSocket() {
    if (!sock_) return;
    sock_->disconnectFromHost();
    if (sock_->state() != QAbstractSocket::UnconnectedState) sock_->abort();
    sock_->deleteLater();
    sock_ = nullptr;
}

void TcpWorker::scheduleReconnect() {
    if (shutting_down_ || host_.isEmpty()) return;
    if (reconnect_scheduled_) return;  // 双路径去重: 只允许一个在飞退避定时器
    reconnect_scheduled_ = true;
    if (sock_) sock_->abort();
    qCInfo(lcNet) << "reconnect in" << backoff_ms_ << "ms";
    QTimer::singleShot(backoff_ms_, this, [this] {
        reconnect_scheduled_ = false;
        if (shutting_down_) return;
        connecting_ = true;
        ensureSocket();
        sock_->connectToHost(host_, port_);
    });
    backoff_ms_ = std::min(backoff_ms_ * 2, kBackoffCapMs);
}

void TcpWorker::flushPending() {
    if (!sock_ || pending_.isEmpty()) return;
    for (const auto& wire : pending_) sock_->write(wire);
    pending_.clear();
}

} // namespace bike::client
