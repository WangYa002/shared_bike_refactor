// bike-dispatch 入口 (Linux-only, 模块三)。
//
// 业务进程: 单 RingReader 线程从 N 个请求环读 IpcPacket → 投递 M 个业务线程
// 执行 Router::dispatch → 响应经 MPSC ReplyQueue 汇聚 → RingReader 批量写
// 响应环(单写者) + rsp_notify FIFO 通知 Gateway。
//
// 判活/恢复(设计稿 §7.6): Gateway 对端心跳超时 → 进程退出, compose 重拉整套;
// 每 1s 刷健康文件供 compose healthcheck。

#include "server/config.hpp"
#include "server/logging.hpp"
#include "server/router.hpp"
#include "server/app/ctx_factory.hpp"
#include "server/util/thread_pool.hpp"

#include "bike/ipc/fifo_channel.hpp"
#include "bike/ipc/ipc_packet.hpp"
#include "bike/ipc/reply_queue.hpp"
#include "bike/ipc/ring_source.hpp"
#include "bike/ipc/shm_region.hpp"
#include "bike/ipc/spsc_ring.hpp"

#include <bike/protocol.hpp>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace bike::server;
using namespace bike::ipc;

static std::atomic<bool> g_stopping{false};
static int g_wake_efd{-1};   // 信号 → RingReader 唤醒(async-signal-safe write)

static void on_term(int) {
    g_stopping.store(true, std::memory_order_relaxed);
    if (g_wake_efd >= 0) {
        const std::uint64_t one = 1;
        (void)::write(g_wake_efd, &one, sizeof(one));
    }
}

static void install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);
}

static std::uint64_t steady_now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

static void touch_alive_file(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (f) f << steady_now_ns() << '\n';
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: bike-dispatch <config.toml>\n");
        return 1;
    }
    Config cfg;
    try {
        cfg = load_config(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "config error: %s\n", e.what());
        return 2;
    }
    // 日志路径覆盖(与 Gateway 日志目录分离; 必须在 init_logging 前)
    if (const char* env = std::getenv("BIKE_LOG_FILE")) {
        if (*env != '\0') cfg.log.file = env;
    }
    init_logging(cfg.log.level, cfg.log.file);

    // 环境变量覆盖(与 Gateway 看到相同值)
    if (const char* env = std::getenv("BIKE_INSTANCE")) {
        const int v = std::atoi(env);
        if (v >= 0 && v <= 99) cfg.ipc.instance = v;
    }
    int req_rings = cfg.uring.workers;   // 请求环个数 = Gateway worker 数
    if (const char* env = std::getenv("BIKE_GATEWAY_WORKERS")) {
        const int v = std::atoi(env);
        if (v > 0) req_rings = v;
    }

    BIKE_LOG_INFO("starting bike-dispatch instance={} req_rings={} biz_workers={}",
                  cfg.ipc.instance, req_rings, cfg.ipc.dispatch_workers);

    // ---- shm 创建/崩溃恢复 + FIFO + attach ----
    ShmRegion::Params sp;
    sp.shm_root = cfg.ipc.shm_root;
    sp.prefix = cfg.ipc.shm_prefix;
    sp.instance = cfg.ipc.instance;
    sp.workers = req_rings;
    ShmRegion shm = ShmRegion::create_or_recover(sp);

    FifoChannel req_notify = FifoChannel::create_or_open(shm.req_notify_path());
    FifoChannel rsp_notify = FifoChannel::create_or_open(shm.rsp_notify_path());

    auto consumers = shm.attach_req_consumers();
    RspRing::Producer rsp_prod = shm.attach_rsp_producer();
    const std::uint32_t pid = static_cast<std::uint32_t>(::getpid());
    for (auto& c : consumers) c.register_pid(pid);
    rsp_prod.register_pid(pid);

    // ---- 业务装配(RideSessionStore 归本进程) ----
    // 声明顺序即析构逆序: biz 最后声明→最先析构(join 业务线程),
    // 保证在途任务引用的 ctx/router/replies 析构前线程已全部退出。
    Ctx ctx = make_prod_ctx(cfg);
    Router router;
    register_all_handlers(router);
    ReplyQueue replies;
    ThreadPool biz(static_cast<std::size_t>(cfg.ipc.dispatch_workers));
    g_wake_efd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (g_wake_efd < 0) {
        BIKE_LOG_ERROR("eventfd(wake) failed");
        return 3;
    }
    install_signal_handlers();

    RingSource source(std::move(consumers), &req_notify, g_wake_efd,
                      static_cast<std::uint32_t>(cfg.ipc.spin_tries));

    const std::string alive_path =
        "/tmp/bike-dispatch" + std::to_string(cfg.ipc.instance) + ".alive";

    // ---- 响应冲刷: ReplyQueue(MPSC) → 响应环(单写者) ----
    auto flush_replies = [&]() {
        std::vector<ReplyItem> items;
        replies.drain(items);
        if (items.empty()) return;
        const bool was_empty = rsp_prod.empty();
        std::uint32_t pending = 0;
        std::size_t i = 0;
        // 满环自旋退出条件: 停机 或 超过 peer_timeout+1s(视为 Gateway 已死,
        // 丢弃尾部响应回到主循环走判活/退出路径, 避免卡死在自旋内)。
        const auto full_deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(cfg.ipc.peer_timeout_ms + 1000);
        while (i < items.size()) {
            RspRing::Slot* slot = rsp_prod.begin_write();
            if (slot == nullptr) {
                // 响应环满: 先发布已填部分, 再等待 Gateway 消费
                rsp_prod.publish(pending);
                pending = 0;
                if (g_stopping.load(std::memory_order_relaxed) ||
                    std::chrono::steady_clock::now() > full_deadline) {
                    BIKE_LOG_ERROR("rsp ring full too long; dropping {} replies",
                                   items.size() - i);
                    break;
                }
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#else
                std::this_thread::yield();
#endif
                continue;
            }
            const ReplyItem& r = items[i];
            if (r.frame.size() > RspRing::kPayloadMax) {
                BIKE_LOG_ERROR("reply frame too large ({} > {}), dropping conn={}",
                               r.frame.size(), RspRing::kPayloadMax, r.conn_id);
                ++i;
                continue;
            }
            IpcPacket pkt;
            pkt.conn_id = r.conn_id;
            pkt.seq = r.seq;
            pkt.payload_len = static_cast<std::uint32_t>(r.frame.size());
            RspRing::Producer::fill(slot, pkt, r.frame.data());
            ++pending;
            ++i;
        }
        if (pending > 0) rsp_prod.publish(pending);
        if (was_empty) rsp_notify.write1();
    };

    // ---- RingReader 主循环 ----
    std::thread reader([&]() {
        std::uint64_t last_hb_ns = 0;
        std::uint64_t last_metrics_ns = 0;
        std::uint64_t consumed_total = 0;      // 周期指标: 累计消费请求数
        std::uint64_t flush_total = 0;         // 周期指标: 累计冲刷响应批次数
        std::size_t reply_peak = 0;            // 周期指标: ReplyQueue 深度峰值
        bool peer_seen = false;
        while (!g_stopping.load(std::memory_order_relaxed)) {
          try {
            // 有待冲刷响应时缩短 poll 切片, 压低响应回流延迟
            const auto slice = replies.size() > 0
                ? std::chrono::milliseconds(5)
                : std::chrono::milliseconds(200);

            source.wait_pop([&](const ReqRing::Slot& slot) {
                // cb 不得上抛: RingSource 在 cb 正常返回后才归还槽,
                // 异常上抛 → 槽泄漏且裸线程 terminate。
                const IpcPacket& pkt = slot.pkt;
                if (pkt.payload_len > ReqRing::kPayloadMax) return;  // 防御
                const bool one_way = (pkt.flags & kFlagOneWay) != 0;
                // 槽归还前拷走 payload(业务线程异步执行)
                std::string payload;
                try {
                    payload.assign(reinterpret_cast<const char*>(slot.payload),
                                   pkt.payload_len);
                } catch (const std::exception& e) {
                    BIKE_LOG_ERROR("req payload alloc failed ({}), dropping: {}",
                                   pkt.payload_len, e.what());
                    return;
                }
                const std::uint64_t conn_id = pkt.conn_id;
                const std::uint16_t eid = pkt.event_id;
                const std::uint32_t seq = pkt.seq;
                try {
                    biz.post([&router, &ctx, &replies, conn_id, eid, seq, one_way,
                              payload = std::move(payload)]() {
                        std::vector<std::uint8_t> frame;
                        try {
                            frame = router.dispatch(eid, payload, ctx);
                        } catch (const std::exception& e) {
                            BIKE_LOG_ERROR("handler eid={} conn={} threw: {}",
                                           eid, conn_id, e.what());
                            return;
                        } catch (...) {
                            BIKE_LOG_ERROR("handler eid={} conn={} threw non-std",
                                           eid, conn_id);
                            return;
                        }
                        if (one_way || frame.empty()) return;
                        // Dispatch 侧 stamp_seq(ring 模式 Gateway 不再回带)
                        if (!bike::stamp_seq(frame, seq)) {
                            BIKE_LOG_ERROR("stamp_seq failed eid={} conn={}", eid, conn_id);
                            return;
                        }
                        replies.push(ReplyItem{conn_id, seq, one_way, std::move(frame)});
                    });
                } catch (const std::exception& e) {
                    // std::function 构造/队列投递抛 bad_alloc 等: 降级丢弃该请求
                    BIKE_LOG_ERROR("biz.post failed eid={} conn={}, dropping: {}",
                                   eid, conn_id, e.what());
                } catch (...) {
                    BIKE_LOG_ERROR("biz.post failed eid={} conn={}, dropping (non-std)",
                                   eid, conn_id);
                }
                ++consumed_total;
            }, slice);

            // 周期指标水位采样 + 冲刷响应环
            if (const std::size_t qn = replies.size(); qn > reply_peak) reply_peak = qn;
            flush_replies();
            ++flush_total;

            // ---- 1s 节流: 心跳 + Gateway 判活 + 健康文件 ----
            const std::uint64_t now = steady_now_ns();
            if (now - last_hb_ns >= 1000000000ull) {
                last_hb_ns = now;
                rsp_prod.heartbeat(now);
                source.heartbeat_all(now);
                touch_alive_file(alive_path);
                const bool alive = source.producer_alive(
                    std::chrono::milliseconds(cfg.ipc.peer_timeout_ms));
                if (alive) {
                    peer_seen = true;
                } else if (peer_seen) {
                    // 曾见过 Gateway 心跳但已超时: 对端死亡 → 退出由 compose 重拉
                    BIKE_LOG_ERROR("gateway peer dead; dispatch exiting for relaunch");
                    g_stopping.store(true, std::memory_order_relaxed);
                }
            }
            // ---- 30s 节流: 周期指标汇总(设计稿 §9.4) ----
            if (now - last_metrics_ns >= 30000000000ull) {
                last_metrics_ns = now;
                BIKE_LOG_INFO("dispatch metrics: consumed_total={} reply_queue_peak={} "
                              "reply_queue_now={} flush_total={}",
                              consumed_total, reply_peak, replies.size(), flush_total);
            }
          } catch (const std::exception& e) {
            // reader 主循环兜底: 记日志后继续, 防裸线程 terminate
            BIKE_LOG_ERROR("reader loop exception: {}", e.what());
          } catch (...) {
            BIKE_LOG_ERROR("reader loop unknown exception");
          }
        }
    });

    BIKE_LOG_INFO("bike-dispatch running: instance={} biz_workers={}",
                  cfg.ipc.instance, cfg.ipc.dispatch_workers);
    reader.join();
    // biz 析构 join 业务线程; reader 已停, 未冲刷的尾部响应随停机丢弃(可接受)
    BIKE_LOG_INFO("bike-dispatch stopped");
    return 0;
}
