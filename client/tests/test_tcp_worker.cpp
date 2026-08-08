// TcpWorker 单元测试:
// 1) 纯函数 consume_frames: 粘包 / 半包 / 逐字节 / 坏 magic / 坏 len —— 无需 socket;
// 2) 集成: TcpWorker(moveToThread) + 本机 QTcpServer 回环,
//    验证 connected / frameReady / 坏 magic 断连。
#include "net/tcp_worker.hpp"

#include <bike/protocol.hpp>

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>

#include <tuple>
#include <vector>

using bike::client::consume_frames;
using bike::client::TcpWorker;

namespace {

QByteArray encodeFrame(quint16 eid, quint32 seq, const QByteArray& payload) {
    bike::Frame f;
    f.event_id = eid;
    f.seq = seq;
    f.payload = std::string(payload.constData(), payload.size());
    auto bytes = bike::encode(f);
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<int>(bytes.size()));
}

struct Got {
    quint16 eid;
    quint32 seq;
    QByteArray payload;
};

} // namespace

// ===================== 纯函数切帧 =====================

TEST(ConsumeFrames, GluedFramesAreSplit) {
    // 两个完整帧粘在一起, 一次投递应切出 2 帧。
    QByteArray buf = encodeFrame(0x02, 7, "hello") + encodeFrame(0x04, 8, "world!");
    std::vector<Got> got;
    const bool ok = consume_frames(buf, [&](quint16 e, quint32 s, const QByteArray& p) {
        got.push_back({e, s, p});
    });
    EXPECT_TRUE(ok);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].eid, 0x02);
    EXPECT_EQ(got[0].seq, 7u);
    EXPECT_EQ(got[0].payload, QByteArray("hello"));
    EXPECT_EQ(got[1].eid, 0x04);
    EXPECT_EQ(got[1].seq, 8u);
    EXPECT_EQ(got[1].payload, QByteArray("world!"));
    EXPECT_TRUE(buf.isEmpty());
}

TEST(ConsumeFrames, HalfPacketIsKept) {
    // 半包: 先投 10 字节(< 帧头 14), 应无产出且缓冲原样保留; 补齐后切出 1 帧。
    const QByteArray full = encodeFrame(0x08, 42, "payload123");
    QByteArray buf = full.left(10);
    int n = 0;
    EXPECT_TRUE(consume_frames(buf, [&](quint16, quint32, const QByteArray&) { ++n; }));
    EXPECT_EQ(n, 0);
    EXPECT_EQ(buf.size(), 10);

    buf.append(full.mid(10));
    EXPECT_TRUE(consume_frames(buf, [&](quint16, quint32, const QByteArray&) { ++n; }));
    EXPECT_EQ(n, 1);
    EXPECT_TRUE(buf.isEmpty());
}

TEST(ConsumeFrames, ByteByByteStream) {
    // 逐字节投递也要能切出完整帧。
    const QByteArray full = encodeFrame(0x12, 5, "ab");
    QByteArray buf;
    int n = 0;
    for (int i = 0; i < full.size(); ++i) {
        buf.append(full.mid(i, 1));
        EXPECT_TRUE(consume_frames(buf, [&](quint16, quint32, const QByteArray&) { ++n; }));
    }
    EXPECT_EQ(n, 1);
    EXPECT_TRUE(buf.isEmpty());
}

TEST(ConsumeFrames, BadMagicReportsProtocolError) {
    QByteArray buf("XXXXnot-a-valid-frame");
    int n = 0;
    EXPECT_FALSE(consume_frames(buf, [&](quint16, quint32, const QByteArray&) { ++n; }));
    EXPECT_EQ(n, 0);
}

TEST(ConsumeFrames, BadLengthReportsProtocolError) {
    // 合法 magic 但 len = -1 → 坏帧。
    QByteArray buf = encodeFrame(0x02, 1, "x");
    ASSERT_GE(buf.size(), 14);
    buf[10] = static_cast<char>(0xFF);
    buf[11] = static_cast<char>(0xFF);
    buf[12] = static_cast<char>(0xFF);
    buf[13] = static_cast<char>(0xFF);
    int n = 0;
    EXPECT_FALSE(consume_frames(buf, [&](quint16, quint32, const QByteArray&) { ++n; }));
    EXPECT_EQ(n, 0);
}

TEST(ConsumeFrames, FrameAfterHalfHeaderWaits) {
    // 完整帧 + 下一帧的半个帧头: 切出 1 帧, 剩余 5 字节保留。
    QByteArray buf = encodeFrame(0x06, 3, "ok") + QByteArray("FBEBX");
    int n = 0;
    EXPECT_TRUE(consume_frames(buf, [&](quint16, quint32, const QByteArray&) { ++n; }));
    EXPECT_EQ(n, 1);
    EXPECT_EQ(buf.size(), 5);
}

// ===================== 集成: 回环 socket =====================

TEST(TcpWorkerIntegration, ConnectFrameAndBadMagicDisconnect) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    QThread thread;
    TcpWorker worker;  // 无 parent
    worker.moveToThread(&thread);
    thread.start();

    QSignalSpy connectedSpy(&worker, &TcpWorker::connected);
    QSignalSpy frameSpy(&worker, &TcpWorker::frameReady);
    QSignalSpy disconnectedSpy(&worker, &TcpWorker::disconnected);

    const quint16 port = server.serverPort();
    QMetaObject::invokeMethod(&worker, [&worker, port] {
        worker.connectToServer(QStringLiteral("127.0.0.1"), port);
    }, Qt::QueuedConnection);

    ASSERT_TRUE(server.waitForNewConnection(5000));
    QTcpSocket* peer = server.nextPendingConnection();
    ASSERT_NE(peer, nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 3000);

    // 半包 + 粘包混合投递: 先投帧 A 的前 8 字节, 再投剩余部分 + 整个帧 B。
    const QByteArray frameA = encodeFrame(0x04, 9, "abc");
    const QByteArray frameB = encodeFrame(0x08, 10, "de");
    peer->write(frameA.left(8));
    peer->waitForBytesWritten(2000);
    peer->write(frameA.mid(8) + frameB);
    peer->waitForBytesWritten(2000);

    QTRY_COMPARE_WITH_TIMEOUT(frameSpy.count(), 2, 3000);
    {
        const auto a = frameSpy.at(0);
        EXPECT_EQ(a.at(0).toUInt(), 0x04u);
        EXPECT_EQ(a.at(1).toUInt(), 9u);
        EXPECT_EQ(a.at(2).toByteArray(), QByteArray("abc"));
        const auto b = frameSpy.at(1);
        EXPECT_EQ(b.at(0).toUInt(), 0x08u);
        EXPECT_EQ(b.at(1).toUInt(), 10u);
        EXPECT_EQ(b.at(2).toByteArray(), QByteArray("de"));
    }

    // 坏 magic: worker 应断开连接。
    peer->write("NOT-FBEB-garbage-stream");
    peer->waitForBytesWritten(2000);
    QTRY_VERIFY_WITH_TIMEOUT(disconnectedSpy.count() >= 1, 3000);

    // 清理: 停止重连并退出 worker 线程。
    QMetaObject::invokeMethod(&worker, [&worker] { worker.shutdown(); },
                              Qt::QueuedConnection);
    QThread::msleep(50);
    thread.quit();
    EXPECT_TRUE(thread.wait(3000));
    delete peer;
}

// ===================== 集成: 坏帧断连后自动重连并恢复收帧 =====================
// 回归 #1: 旧实现坏帧路径仅 disconnect(this) 剪断信号, 退避重连里 ensureSocket
// 因 sock_ 非空直接复用旧 socket 且不重挂信号 → 重连成功但无人监听, 永久失联。
TEST(TcpWorkerIntegration, BadFrameReconnectAndReceiveAgain) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    QThread thread;
    TcpWorker worker;  // 无 parent
    worker.moveToThread(&thread);
    thread.start();

    QSignalSpy connectedSpy(&worker, &TcpWorker::connected);
    QSignalSpy frameSpy(&worker, &TcpWorker::frameReady);
    QSignalSpy disconnectedSpy(&worker, &TcpWorker::disconnected);

    const quint16 port = server.serverPort();
    QMetaObject::invokeMethod(&worker, [&worker, port] {
        worker.connectToServer(QStringLiteral("127.0.0.1"), port);
    }, Qt::QueuedConnection);

    // ---- 第一次连接: 服务端先发坏 magic 帧 ----
    ASSERT_TRUE(server.waitForNewConnection(5000));
    QTcpSocket* peer1 = server.nextPendingConnection();
    ASSERT_NE(peer1, nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 3000);

    peer1->write("NOT-FBEB-bad-magic");
    peer1->waitForBytesWritten(2000);
    QTRY_VERIFY_WITH_TIMEOUT(disconnectedSpy.count() >= 1, 3000);

    // ---- 退避重连(500ms 起)后应出现第二次连接 ----
    QTcpSocket* peer2 = nullptr;
    if (server.hasPendingConnections()) {
        peer2 = server.nextPendingConnection();
    } else {
        if (server.waitForNewConnection(5000))
            peer2 = server.nextPendingConnection();
    }
    ASSERT_NE(peer2, nullptr);
    // 关键断言: 重连后 connected 信号再次发出(旧 socket 被丢弃并重建挂信号)
    QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 2, 3000);

    // ---- 新连接上发好帧, 必须能切出并送达 ----
    peer2->write(encodeFrame(0x08, 21, "after-reconnect"));
    peer2->waitForBytesWritten(2000);
    QTRY_COMPARE_WITH_TIMEOUT(frameSpy.count(), 1, 3000);
    {
        const auto f = frameSpy.at(0);
        EXPECT_EQ(f.at(0).toUInt(), 0x08u);
        EXPECT_EQ(f.at(1).toUInt(), 21u);
        EXPECT_EQ(f.at(2).toByteArray(), QByteArray("after-reconnect"));
    }

    // 清理
    QMetaObject::invokeMethod(&worker, [&worker] { worker.shutdown(); },
                              Qt::QueuedConnection);
    QThread::msleep(50);
    thread.quit();
    EXPECT_TRUE(thread.wait(3000));
    delete peer1;
    delete peer2;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
