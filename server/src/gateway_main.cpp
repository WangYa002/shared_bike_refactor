// bike-gateway 入口 (Linux-only, 模块三)。
//
// 两种模式(由 [ipc].mode 选择):
//   ring      : 纯网关。请求经 RingSink 写 per-worker mmap SPSC 环 → Dispatch 进程;
//               响应经响应环 + rsp_notify FIFO 回流, 引擎零解析直发客户端。
//               本进程不装配任何业务 Ctx/handler。
//   inprocess : 回退模式。进程内直连 Router(等价旧 bike-server 单进程形态)。
//
// 环境变量: BIKE_GATEWAY_WORKERS 覆盖 worker 数; BIKE_INSTANCE 覆盖 IPC 实例号
// (compose 多实例并存时区分 shm/FIFO 命名, Gateway 与 Dispatch 必须一致)。

#include "server/config.hpp"
#include "server/logging.hpp"
#include "server/router.hpp"
#include "server/app/ctx_factory.hpp"
#include "server/gateway/uring_engine.hpp"
#include "server/gateway/packet_sink.hpp"

#include "bike/ipc/fifo_channel.hpp"
#include "bike/ipc/ring_sink.hpp"
#include "bike/ipc/shm_region.hpp"

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

using namespace bike::server;

// 信号处理只置原子标志 + write(stop eventfd), 均为 async-signal-safe。
static std::atomic<bike::gateway::UringEngine*> g_engine{nullptr};
static void on_term(int) {
    auto* e = g_engine.load(std::memory_order_acquire);
    if (e != nullptr) e->request_stop();
}

// sigaction: 语义跨平台一致(不自动重置为 SIG_DFL); 不加 SA_RESTART,
// 让阻塞的系统调用尽快返回主循环。
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

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: bike-gateway <config.toml>\n");
        return 1;
    }
    Config cfg;
    try {
        cfg = load_config(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "config error: %s\n", e.what());
        return 2;
    }
    init_logging(cfg.log.level, cfg.log.file);

    // 环境变量覆盖(双进程必须看到相同值)
    if (const char* env = std::getenv("BIKE_INSTANCE")) {
        const int v = std::atoi(env);
        if (v >= 0 && v <= 99) cfg.ipc.instance = v;
    }

    BIKE_LOG_INFO("starting bike-gateway mode={} instance={} listening {}:{}",
                  cfg.ipc.mode, cfg.ipc.instance, cfg.server.listen, cfg.server.port);

    // 网关装配: [uring] 段 + BIKE_GATEWAY_WORKERS 覆盖
    bike::gateway::GatewayOptions opt;
    opt.listen = cfg.server.listen;
    opt.port = cfg.server.port;
    opt.sq_depth = cfg.uring.sq_depth;
    opt.cq_depth = cfg.uring.cq_depth;
    opt.workers = cfg.uring.workers;
    if (const char* env = std::getenv("BIKE_GATEWAY_WORKERS")) {
        const int v = std::atoi(env);
        if (v > 0) opt.workers = v;
    }
    opt.rx_buf_bytes = static_cast<std::size_t>(cfg.uring.rx_buf_bytes);
    opt.send_backlog_limit = static_cast<std::size_t>(cfg.uring.send_backlog_limit_bytes);
    opt.idle_timeout = std::chrono::milliseconds(cfg.uring.idle_timeout_ms);
    opt.accept_backlog = cfg.uring.accept_backlog;

    if (cfg.ipc.mode == "ring") {
        // ---- ring 模式: 附着 shm, 业务全在 Dispatch 进程 ----
        bike::ipc::ShmRegion::Params sp;
        sp.shm_root = cfg.ipc.shm_root;
        sp.prefix = cfg.ipc.shm_prefix;
        sp.instance = cfg.ipc.instance;
        sp.workers = opt.workers;   // 请求环个数 = worker 数, 必须与 Dispatch 一致
        sp.open_timeout = std::chrono::milliseconds(cfg.ipc.open_timeout_ms);
        if (const char* env = std::getenv("BIKE_IPC_MLOCK"))
            sp.lock_pages = std::atoi(env) != 0;   // 调参/压测逃生阀, 默认锁定
        bike::ipc::ShmRegion shm = bike::ipc::ShmRegion::open(sp);

        auto producers = shm.attach_req_producers();
        auto rsp_cons = shm.attach_rsp_consumer();
        const std::uint32_t pid = static_cast<std::uint32_t>(::getpid());
        for (auto& p : producers) p.register_pid(pid);
        rsp_cons.register_pid(pid);

        bike::ipc::FifoChannel req_notify =
            bike::ipc::FifoChannel::create_or_open(shm.req_notify_path());
        bike::ipc::FifoChannel rsp_notify =
            bike::ipc::FifoChannel::create_or_open(shm.rsp_notify_path());

        auto sink = std::make_unique<bike::ipc::RingSink>(
            std::move(producers), &req_notify);
        bike::ipc::RingSink* sink_ptr = sink.get();

        // ring 模式下引擎不碰 Router/Ctx(占位引用)
        Router router;
        Ctx ctx;
        bike::gateway::UringEngine engine(
            opt, router, ctx, std::move(sink));
        engine.attach_ipc(std::move(rsp_cons), rsp_notify.fd(),
                          std::chrono::milliseconds(cfg.ipc.peer_timeout_ms));

        // 请求环 producer 心跳线程(1s): 供 Dispatch 判活
        std::atomic<bool> hb_stop{false};
        std::thread heartbeat([&] {
            while (!hb_stop.load(std::memory_order_relaxed)) {
                sink_ptr->heartbeat_all(steady_now_ns());
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        });

        g_engine.store(&engine, std::memory_order_release);
        install_signal_handlers();
        BIKE_LOG_INFO("gateway(ring) running: workers={} instance={}",
                      opt.workers, cfg.ipc.instance);
        engine.run();   // 阻塞至优雅停机(含 Dispatch 判死触发的重启停机)
        g_engine.store(nullptr, std::memory_order_release);
        hb_stop.store(true, std::memory_order_relaxed);
        heartbeat.join();
        BIKE_LOG_INFO("bike-gateway stopped");
        return 0;
    }

    // ---- inprocess 回退模式: 等价旧 bike-server 单进程形态 ----
    Ctx ctx = make_prod_ctx(cfg);
    Router router;
    register_all_handlers(router);

    bike::gateway::UringEngine engine(
        opt, router, ctx,
        std::make_unique<bike::gateway::InProcessRouterSink>(router));

    // 先赋值 g_engine 再安装 handler: 避免 handler 已生效但指针尚为空的竞态窗口
    g_engine.store(&engine, std::memory_order_release);
    install_signal_handlers();
    BIKE_LOG_INFO("gateway(inprocess) running: workers={}", opt.workers);
    engine.run();
    g_engine.store(nullptr, std::memory_order_release);
    BIKE_LOG_INFO("bike-gateway stopped");
    return 0;
}
