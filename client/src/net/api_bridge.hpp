#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QThread;

namespace bike::client {

class TcpWorker;

// QML 侧唯一的业务门面 (context property "api")。
// 线程模型:
//   QML(GUI线程) --调用--> ApiBridge(GUI线程, protobuf 编解码微秒级)
//   ApiBridge --信号 requestSend(Queued)--> TcpWorker(专属线程, socket IO)
//   TcpWorker --信号 frameReady(Queued)--> ApiBridge::onFrameReady
// 全程零锁: 不使用 QMutex / QtConcurrent / std::thread / atomic。
// 会话状态 (token / activeRide) 语义迁移自旧 SessionModel + MainWindow。
class ApiBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY sessionChanged)
    Q_PROPERTY(QString token READ token NOTIFY sessionChanged)
    Q_PROPERTY(QString mobile READ mobile NOTIFY sessionChanged)
    Q_PROPERTY(QString activeRide READ activeRide NOTIFY sessionChanged)
    Q_PROPERTY(bool linkUp READ linkUp NOTIFY linkStateChanged)
public:
    explicit ApiBridge(const QString& host, quint16 port, QObject* parent = nullptr);
    ~ApiBridge() override;

    bool loggedIn() const { return !token_.empty(); }
    QString token() const { return QString::fromStdString(token_); }
    QString mobile() const { return mobile_; }
    QString activeRide() const { return active_ride_; }
    bool linkUp() const { return link_up_; }

    // ---- 12 个异步入口, 全部立即返回, 结果经对应信号送达 ----
    Q_INVOKABLE void sendMobileCode(const QString& mobile);
    Q_INVOKABLE void login(const QString& mobile, int icode);
    Q_INVOKABLE void recharge(int amount_cent);
    Q_INVOKABLE void refreshBalance();
    Q_INVOKABLE void listRecords();
    Q_INVOKABLE void refreshNearbyBikes(double lat, double lng);
    Q_INVOKABLE void scanUnlock(const QString& bike_no, double lat, double lng);
    Q_INVOKABLE void reportPosition(const QString& ride_no, int seq_i,
                                    double lat, double lng, int elapsed_sec);
    Q_INVOKABLE void endRide(double lat, double lng);
    Q_INVOKABLE void reportDamage(const QString& bike_no, const QString& note);
    Q_INVOKABLE void rideDetail(const QString& ride_no);
    Q_INVOKABLE void listRides(int limit);

    // 清理本地未结订单状态(孤儿订单"忽略"语义, 迁移自旧 MainWindow)。
    Q_INVOKABLE void clearActiveRide();

signals:
    void sessionChanged();
    void linkStateChanged();

    // 内部: 跨线程投递到 TcpWorker::sendFrame (自动 QueuedConnection)
    void requestSend(quint16 eid, quint32 seq, const QByteArray& payload);

    // ---- 结果信号 ----
    void mobileCodeResult(bool ok, int icode, const QString& desc);
    void loginResult(bool ok, const QString& desc);
    void balanceReady(bool ok, int balance_cent, const QString& desc);
    void recordsReady(bool ok, const QVariantList& records, const QString& desc);
    void nearbyBikesReady(bool ok, const QVariantList& bikes, const QString& desc);
    void unlockResult(bool ok, const QString& ride_no, const QString& desc);
    void rideEnded(bool ok, int amount_cent, int balance_after, const QString& desc);
    void damageResult(bool ok, const QString& desc);
    void rideDetailReady(bool ok, const QVariantMap& detail, const QString& desc);
    void ridesReady(bool ok, const QVariantList& rides, const QString& desc);

private slots:
    void onFrameReady(quint16 eid, quint32 seq, const QByteArray& payload);
    void onWorkerConnected();
    void onWorkerDisconnected(const QString& reason);

private:
    quint32 nextSeq();
    // 断线/重连时对每个在途请求 eid emit 对应失败 Result 信号并清空,
    // 避免 QML 侧 pending 标志永久卡死。
    void failInFlight(const QString& msg);
    // 单飞槽位: 同请求 eid 在途时拒绝新请求(返回 false), 日志记录。
    bool beginRequest(quint16 req_eid, const char* what);
    template <typename Req>
    void sendRequest(std::uint16_t req_eid, const Req& req, const char* what);
    void handleFrame(quint16 rsp_eid, const QByteArray& payload);

    QString host_;
    quint16 port_;
    QThread*   thread_{nullptr};
    TcpWorker* worker_{nullptr};

    std::string token_;     // 旧 SessionModel::token
    QString mobile_;        // 旧 SessionModel::mobile
    QString pending_mobile_; // 登录请求在途时暂存, 成功后写入 mobile_
    QString active_ride_;   // 旧 MainWindow::active_ride_ (+ QSettings 持久化)
    bool link_up_{false};

    std::uint32_t seq_counter_{0};           // 每连接自增, 回绕跳过 0
    QHash<quint16, quint32> in_flight_;      // 请求 eid -> 在途 seq (单飞 + 校验)
};

} // namespace bike::client
