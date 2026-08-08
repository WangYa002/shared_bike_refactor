#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

class QTcpSocket;

namespace bike::client {

// 纯函数切帧: 从 buf 中消费尽可能多的完整 FBEB v2 帧。
// - 完整帧通过 on_frame 回调逐个交出, buf 前移 consumed 字节;
// - 半包保留在 buf 中等待下次投递;
// - 返回 true = 正常(可能一个帧都没切出来), false = 坏帧(magic/len 非法), 调用方应断连。
// 抽成纯函数以便在无 socket 的单测里验证粘包/半包/坏 magic 行为。
bool consume_frames(
    QByteArray& buf,
    const std::function<void(quint16 eid, quint32 seq, const QByteArray& payload)>& on_frame);

// 长连接 worker: 必须 moveToThread 到专属 QThread 使用(无 parent 构造)。
// 所有 socket 读写只在本对象所在线程发生; 跨线程一律走信号槽(QueuedConnection)。
// 断线后按指数退避(500ms 起, 上限 10s)自动重连, shutdown() 后停止。
class TcpWorker : public QObject {
    Q_OBJECT
public:
    TcpWorker();  // 无 parent, 以支持 moveToThread

signals:
    void connected();
    void disconnected(const QString& reason);
    void frameReady(quint16 eid, quint32 seq, const QByteArray& payload);
    void errorOccurred(const QString& msg);

public slots:
    void connectToServer(const QString& host, quint16 port);
    void sendFrame(quint16 eid, quint32 seq, const QByteArray& payload);
    void shutdown();

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();

private:
    void ensureSocket();
    void dropSocket();
    void scheduleReconnect();
    void flushPending();

    QString  host_;
    quint16  port_{0};
    bool     shutting_down_{false};
    bool     connecting_{false};
    bool     reconnect_scheduled_{false};  // 防止 error/disconnect 双路径重复排程
    int      backoff_ms_{500};
    QTcpSocket* sock_{nullptr};
    QByteArray  rx_;
    QList<QByteArray> pending_;  // 未连接期间缓存的待发帧(上限丢弃最旧)
};

} // namespace bike::client
