// 模块三 Linux 双进程集成测试 (Linux-only)。
//
// fork 出真实双进程 + 真实 shm_open/mmap, 端到端验证:
//   1. Dispatch(creator) create_or_recover 建环, Gateway(opener) open 附着;
//   2. Gateway producer 写请求环 → Dispatch consumer 读;
//   3. Dispatch producer 写响应环 → Gateway consumer 读;
//   4. FIFO 唤醒通道 write1/drain;
//   5. 心跳/判活字段跨进程可见。
//
// Windows 不可构建; 部署阶段在 Docker/Linux 内随 ctest 运行。

#include <gtest/gtest.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "bike/ipc/fifo_channel.hpp"
#include "bike/ipc/ipc_errors.hpp"
#include "bike/ipc/ipc_packet.hpp"
#include "bike/ipc/ring_sink.hpp"
#include "bike/ipc/ring_source.hpp"
#include "bike/ipc/shm_region.hpp"
#include "bike/ipc/spsc_ring.hpp"
#include "server/router.hpp"

using namespace bike::ipc;
using namespace std::chrono_literals;

namespace {

constexpr int kInstance = 77;   // 独立实例号避免与真实部署冲突
constexpr int kWorkers = 2;

ShmRegion::Params make_params() {
    ShmRegion::Params p;
    p.shm_root = "/tmp";
    p.prefix = "bike_ipc_test";
    p.instance = kInstance;
    p.workers = kWorkers;
    p.open_timeout = 5000ms;
    return p;
}

// 测试收尾清理 shm 文件与 FIFO(不影响生产: 生产不主动 unlink)
void cleanup_files() {
    ::shm_unlink(("/bike_ipc_test" + std::to_string(kInstance) + "_req").c_str());
    ::shm_unlink(("/bike_ipc_test" + std::to_string(kInstance) + "_rsp").c_str());
    ::unlink("/tmp/bike_ipc_test77_req_notify");
    ::unlink("/tmp/bike_ipc_test77_rsp_notify");
}

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

// 子进程(模拟 Gateway): 等待父进程建环 → open 附着 → 写请求 → 读响应。
// mode: 0=普通回声(count 条); 1=单向事件(4 条, 断言无响应回流)。
int gateway_child(int mode, std::uint32_t count) {
    try {
        ShmRegion shm = ShmRegion::open(make_params());
        auto producers = shm.attach_req_producers();
        auto rsp_cons = shm.attach_rsp_consumer();
        const std::uint32_t pid = static_cast<std::uint32_t>(::getpid());
        for (auto& p : producers) p.register_pid(pid);
        rsp_cons.register_pid(pid);

        FifoChannel req_notify = FifoChannel::create_or_open(shm.req_notify_path());

        if (mode == 1) {
            // 单向事件: kFlagOneWay 标记, 不应有任何响应回流
            for (std::uint32_t i = 0; i < 4; ++i) {
                IpcPacket pkt;
                pkt.conn_id = 2000 + i;
                pkt.event_id = 0x15;
                pkt.flags |= kFlagOneWay;
                pkt.seq = i;
                pkt.payload_len = sizeof(i);
                while (!producers[0].push(pkt, &i))
                    std::this_thread::sleep_for(1ms);
            }
            req_notify.write1();
            std::this_thread::sleep_for(300ms);
            const RspRing::Slot* batch = nullptr;
            if (rsp_cons.pop_batch(&batch, 4) != 0) return 21;   // 意外响应
            return 0;
        }

        // 写 count 条请求到环 0, payload 携带序号(≥512 即覆盖满/回绕)
        for (std::uint32_t i = 0; i < count; ++i) {
            IpcPacket pkt;
            pkt.conn_id = 1000 + i;
            pkt.event_id = 0x01;
            pkt.seq = i;
            pkt.payload_len = sizeof(i);
            while (!producers[0].push(pkt, &i))
                std::this_thread::sleep_for(1ms);
        }
        req_notify.write1();

        // 等响应环回流 count 条(Dispatch 会把 seq 原样放进 payload)
        std::uint64_t sum = 0;
        std::uint32_t got = 0;
        std::uint64_t expect_sum = 0;
        for (std::uint32_t i = 0; i < count; ++i) expect_sum += i;
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (got < count && std::chrono::steady_clock::now() < deadline) {
            const RspRing::Slot* batch = nullptr;
            const std::uint32_t n = rsp_cons.pop_batch(&batch, 32);
            for (std::uint32_t i = 0; i < n; ++i) {
                std::uint32_t v;
                std::memcpy(&v, batch[i].payload, sizeof(v));
                sum += v;
                ++got;
            }
            if (n > 0) rsp_cons.release(n);
            else std::this_thread::sleep_for(1ms);
        }
        if (got != count) return 11;
        if (sum != expect_sum) return 12;

        // 校验 Dispatch 侧 pid/心跳已注册(跨进程可见)
        if (rsp_cons.producer_pid() == 0) return 13;
        if (rsp_cons.producer_heartbeat() == 0) return 14;
        return 0;
    } catch (...) {
        return 20;
    }
}

// 双进程端到端骨架: echo_n 条回声请求 + one_way_n 条单向(不回响应)。
void run_forked_e2e(std::uint32_t echo_n, std::uint32_t one_way_n) {
    cleanup_files();

    // ---- 父进程 = Dispatch(creator) ----
    ShmRegion shm = ShmRegion::create_or_recover(make_params());
    FifoChannel req_notify = FifoChannel::create_or_open(shm.req_notify_path());
    FifoChannel rsp_notify = FifoChannel::create_or_open(shm.rsp_notify_path());

    auto consumers = shm.attach_req_consumers();
    RspRing::Producer rsp_prod = shm.attach_rsp_producer();
    const std::uint32_t dpid = static_cast<std::uint32_t>(::getpid());
    for (auto& c : consumers) c.register_pid(dpid);
    rsp_prod.register_pid(dpid);
    rsp_prod.heartbeat(now_ns());

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        ::_exit(gateway_child(one_way_n > 0 ? 1 : 0, echo_n));
    }

    // ---- Dispatch 侧: 收请求 → 非单向才回声写响应环 ----
    std::uint32_t got = 0;
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (got < echo_n && std::chrono::steady_clock::now() < deadline) {
        for (auto& cons : consumers) {
            const ReqRing::Slot* batch = nullptr;
            const std::uint32_t n = cons.pop_batch(&batch, 32);
            for (std::uint32_t i = 0; i < n; ++i) {
                if (batch[i].pkt.flags & kFlagOneWay) continue;   // 单向: 无响应
                // 响应 = 请求 payload 原样回投(测试用 echo 协议)
                IpcPacket rp;
                rp.conn_id = batch[i].pkt.conn_id;
                rp.seq = batch[i].pkt.seq;
                rp.payload_len = batch[i].pkt.payload_len;
                while (!rsp_prod.push(rp, batch[i].payload))
                    std::this_thread::sleep_for(1ms);
                ++got;
            }
            if (n > 0) {
                cons.release(n);
                rsp_notify.write1();
            }
        }
        if (got < echo_n) {
            req_notify.drain();
            rsp_prod.heartbeat(now_ns());
            std::this_thread::sleep_for(1ms);
        }
    }
    ASSERT_EQ(got, echo_n);

    // 维持心跳让子进程判活通过
    rsp_prod.heartbeat(now_ns());

    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);

    cleanup_files();
}

} // namespace

TEST(IpcLinux, TwoProcessEndToEnd) {
    run_forked_e2e(/*echo_n=*/8, /*one_way_n=*/0);
}

// 5e: 请求量 ≥1000 条, 覆盖环满/回绕(ReqRing 512 槽, RspRing 256 槽)
TEST(IpcLinux, TwoProcessEndToEndWraparound) {
    run_forked_e2e(/*echo_n=*/1000, /*one_way_n=*/0);
}

// 5a: 单向事件 kFlagOneWay —— Dispatch 不产生任何响应回流
TEST(IpcLinux, OneWayNoResponseBackflow) {
    cleanup_files();
    ShmRegion shm = ShmRegion::create_or_recover(make_params());
    FifoChannel req_notify = FifoChannel::create_or_open(shm.req_notify_path());
    FifoChannel rsp_notify = FifoChannel::create_or_open(shm.rsp_notify_path());
    auto consumers = shm.attach_req_consumers();
    RspRing::Producer rsp_prod = shm.attach_rsp_producer();
    const std::uint32_t dpid = static_cast<std::uint32_t>(::getpid());
    for (auto& c : consumers) c.register_pid(dpid);
    rsp_prod.register_pid(dpid);
    rsp_prod.heartbeat(now_ns());

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) ::_exit(gateway_child(/*mode=*/1, 0));

    // 消费并丢弃单向请求, 绝不写响应环
    std::uint32_t seen = 0;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (seen < 4 && std::chrono::steady_clock::now() < deadline) {
        for (auto& cons : consumers) {
            const ReqRing::Slot* batch = nullptr;
            const std::uint32_t n = cons.pop_batch(&batch, 8);
            seen += n;
            if (n > 0) cons.release(n);
        }
        req_notify.drain();
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_EQ(seen, 4u);

    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);   // 子进程确认无响应回流
    cleanup_files();
}

// 5b: RingSink 层异常路径 —— payload 超限 MalformedIpcRequest + 满环 SinkOverload
TEST(IpcLinux, RingSinkMalformedAndOverload) {
    cleanup_files();
    ShmRegion shm = ShmRegion::create_or_recover(make_params());
    FifoChannel req_notify = FifoChannel::create_or_open(shm.req_notify_path());
    RingSink sink(shm.attach_req_producers(), &req_notify);
    bike::server::Ctx ctx;
    using bike::gateway::PacketSink;

    // payload 超限(kReqPayloadMax = 4064)
    PacketSink::Request big;
    big.conn_id = 1;
    big.payload.assign(ReqRing::kPayloadMax + 1, 'x');
    EXPECT_THROW(sink.handle(big, ctx), MalformedIpcRequest);

    // 填满请求环 0(512 槽)后第 513 条抛 SinkOverload
    PacketSink::Request req;
    req.conn_id = 1;
    req.event_id = 0x01;
    req.payload.assign(8, 'a');
    for (std::uint32_t i = 0; i < ReqRing::kSlotCount; ++i) {
        req.seq = i;
        sink.handle(req, ctx);
    }
    req.seq = ReqRing::kSlotCount;
    EXPECT_THROW(sink.handle(req, ctx), SinkOverload);
    EXPECT_GE(sink.overload_count(), 1u);
    EXPECT_GT(sink.inflight_peak(), 0u);
    cleanup_files();
}

// 5c: fork 子进程模拟 kill -9 退出 → 父侧 producer_alive 心跳超时判死链路
TEST(IpcLinux, PeerDeathByHeartbeatTimeout) {
    cleanup_files();
    ShmRegion shm = ShmRegion::create_or_recover(make_params());
    FifoChannel req_notify = FifoChannel::create_or_open(shm.req_notify_path());
    auto consumers = shm.attach_req_consumers();
    const std::uint32_t dpid = static_cast<std::uint32_t>(::getpid());
    for (auto& c : consumers) c.register_pid(dpid);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        // 模拟 Gateway producer: 注册 pid + 心跳一次, 随即被 SIGKILL 硬杀
        auto prods = shm.attach_req_producers();
        const std::uint32_t pid = static_cast<std::uint32_t>(::getpid());
        for (auto& p : prods) p.register_pid(pid);
        const std::uint64_t now = now_ns();
        for (auto& p : prods) p.heartbeat(now);
        ::kill(::getpid(), SIGKILL);
        ::_exit(30);   // 不可达
    }

    RingSource source(std::move(consumers), &req_notify, /*wake_fd=*/-1,
                      /*spin_tries=*/16);
    // 等子进程完成 pid/心跳注册(跨进程时序竞态), 随后心跳新鲜期判活为真
    {
        const auto t0 = std::chrono::steady_clock::now();
        while (!source.producer_alive(1000ms) &&
               std::chrono::steady_clock::now() - t0 < 3s)
            std::this_thread::sleep_for(1ms);
        EXPECT_TRUE(source.producer_alive(1000ms));
    }

    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFSIGNALED(status));
    ASSERT_EQ(WTERMSIG(status), SIGKILL);

    // 心跳停止刷新 → 陈旧超阈后判死(pid 已死 + 心跳老化双路径)
    std::this_thread::sleep_for(1500ms);
    EXPECT_FALSE(source.producer_alive(1000ms));
    cleanup_files();
}

// 5d: 空环睡眠 → 写入+通知 → <50ms 唤醒(FIFO 唤醒延迟断言)
TEST(IpcLinux, FifoWakeLatency) {
    cleanup_files();
    ShmRegion shm = ShmRegion::create_or_recover(make_params());
    FifoChannel req_notify = FifoChannel::create_or_open(shm.req_notify_path());
    auto prods = shm.attach_req_producers();
    RingSource source(shm.attach_req_consumers(), &req_notify, /*wake_fd=*/-1,
                      /*spin_tries=*/16);

    std::atomic<std::int64_t> wake_ns{-1};
    std::thread th([&]() {
        const auto t0 = std::chrono::steady_clock::now();
        source.wait_pop([](const ReqRing::Slot&) {},
                        std::chrono::milliseconds(200));
        wake_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - t0).count(),
                      std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(30ms);   // 确保消费线程已进入 poll 睡眠
    const std::uint32_t v = 42;
    IpcPacket pkt;
    pkt.conn_id = 1;
    pkt.seq = 1;
    pkt.payload_len = sizeof(v);
    ASSERT_TRUE(prods[0].push(pkt, &v));
    req_notify.write1();

    th.join();
    const std::int64_t ns = wake_ns.load(std::memory_order_relaxed);
    ASSERT_GE(ns, 0);
    // 空环睡眠 → 写入+write1 → 唤醒, 延迟应远小于 poll 切片(200ms)
    EXPECT_LT(ns, static_cast<std::int64_t>(50 * 1000000))
        << "wake latency " << ns / 1000000 << "ms >= 50ms";
    cleanup_files();
}

TEST(IpcLinux, CreateOrRecoverStaleRegion) {
    // 残留恢复: 先建再"遗留"(不 unlink), 二次 create_or_recover 应成功重建
    cleanup_files();
    {
        ShmRegion shm = ShmRegion::create_or_recover(make_params());
        auto prods_view = shm.attach_req_producers();
        ASSERT_EQ(prods_view.size(), static_cast<std::size_t>(kWorkers));
    }   // 析构只 unmap, 不 unlink → 残留
    {
        ShmRegion shm = ShmRegion::create_or_recover(make_params());
        auto cons = shm.attach_req_consumers();
        ASSERT_EQ(cons.size(), static_cast<std::size_t>(kWorkers));
        for (auto& c : cons) EXPECT_TRUE(c.empty());   // 重建后为空环
    }
    cleanup_files();
}

TEST(IpcLinux, FifoNotifyRoundtrip) {
    const std::string path = "/tmp/bike_ipc_test_fifo_rt";
    ::unlink(path.c_str());
    FifoChannel a = FifoChannel::create_or_open(path);
    FifoChannel b = FifoChannel::create_or_open(path);   // 同路径二次打开
    ASSERT_TRUE(a.write1());
    ASSERT_TRUE(a.write1());
    std::this_thread::sleep_for(10ms);
    EXPECT_GE(b.drain(), 2u);
    EXPECT_EQ(b.drain(), 0u);   // 已读空
    ::unlink(path.c_str());
}
