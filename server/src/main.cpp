#include "server/config.hpp"
#include "server/logging.hpp"
#include "server/router.hpp"
#include "server/handlers.hpp"
#include "server/db/mysql_pool.hpp"
#include "server/db/mysql_user_repo.hpp"
#include "server/db/mysql_account_repo.hpp"
#include "server/db/mysql_bike_repo.hpp"
#include "server/db/mysql_ride_repo.hpp"
#include "server/cache/redis_session_store.hpp"
#include "server/gateway/uring_engine.hpp"
#include "server/gateway/packet_sink.hpp"

#include <signal.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace bike::server;

// 信号处理只置原子标志 + write(stop eventfd), 均为 async-signal-safe。
// g_engine 用 atomic: handler 与装配代码分属不同执行流, 裸指针存在竞态。
static std::atomic<bike::gateway::UringEngine*> g_engine{nullptr};
static void on_term(int) {
    auto* e = g_engine.load(std::memory_order_acquire);
    if (e != nullptr) e->request_stop();
}

// 用 sigaction 替代 std::signal: 语义跨平台一致(不自动重置为 SIG_DFL),
// 且可显式控制 mask/flags。不加 SA_RESTART, 让阻塞的系统调用尽快返回主循环。
static void install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: bike-server <config.toml>\n");
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
    BIKE_LOG_INFO("starting bike-server (io_uring gateway), listening {}:{}",
                  cfg.server.listen, cfg.server.port);

    auto mysql_pool = std::make_shared<MysqlPool>(MysqlPool::Config{
        .host = cfg.mysql.host,
        .port = cfg.mysql.port,
        .user = cfg.mysql.user,
        .password = cfg.mysql.password,
        .database = cfg.mysql.database,
        .pool_size = cfg.mysql.pool_size,
    });

    Ctx ctx{
        .users         = std::make_shared<MysqlUserRepo>(mysql_pool),
        .accounts      = std::make_shared<MysqlAccountRepo>(mysql_pool),
        .sessions      = std::make_shared<RedisSessionStore>(cfg.redis.host, cfg.redis.port, cfg.redis.pool_size),
        .bikes         = std::make_shared<MysqlBikeRepo>(mysql_pool),
        .rides         = std::make_shared<MysqlRideRepo>(mysql_pool),
        .ride_sessions = std::make_shared<RideSessionStore>(),
    };

    Router router;
    router.register_handler(bike::event_id(bike::Event::MobileRequest), handlers::mobile_code);
    router.register_handler(bike::event_id(bike::Event::LoginRequest), handlers::login);
    router.register_handler(bike::event_id(bike::Event::RechargeRequest), handlers::recharge);
    router.register_handler(bike::event_id(bike::Event::AccountBalanceRequest), handlers::account_balance);
    router.register_handler(bike::event_id(bike::Event::ListAccountRecordsRequest), handlers::list_records);
    router.register_handler(bike::event_id(bike::Event::ListNearbyBikesRequest), handlers::list_nearby_bikes);
    router.register_handler(bike::event_id(bike::Event::ScanUnlockRequest), handlers::scan_unlock);
    router.register_handler(bike::event_id(bike::Event::RidePositionReport), handlers::position_report);
    router.register_handler(bike::event_id(bike::Event::EndRideRequest), handlers::end_ride);
    router.register_handler(bike::event_id(bike::Event::ReportDamageRequest), handlers::report_damage);
    router.register_handler(bike::event_id(bike::Event::GetRideDetailRequest), handlers::get_ride_detail);
    router.register_handler(bike::event_id(bike::Event::ListRidesRequest), handlers::list_rides);

    // 网关装配: [uring] 段 + BIKE_GATEWAY_WORKERS 环境变量覆盖(不重编译调参)
    bike::gateway::GatewayOptions opt;
    opt.listen = cfg.server.listen;
    opt.port = cfg.server.port;
    opt.sq_depth = cfg.uring.sq_depth;
    opt.cq_depth = cfg.uring.cq_depth;
    opt.workers = cfg.uring.workers;
    if (const char* env = std::getenv("BIKE_GATEWAY_WORKERS")) {
        int v = std::atoi(env);
        if (v > 0) opt.workers = v;
    }
    opt.rx_buf_bytes = static_cast<std::size_t>(cfg.uring.rx_buf_bytes);
    opt.send_backlog_limit = static_cast<std::size_t>(cfg.uring.send_backlog_limit_bytes);
    opt.idle_timeout = std::chrono::milliseconds(cfg.uring.idle_timeout_ms);
    opt.accept_backlog = cfg.uring.accept_backlog;

    // Step 2: 进程内直连 Router 的 stub sink; Step 3 换 RingDispatchSink(mmap SPSC 环)。
    bike::gateway::UringEngine engine(
        opt, router, ctx,
        std::make_unique<bike::gateway::InProcessRouterSink>(router));

    // 先赋值 g_engine 再安装 handler: 避免 handler 已生效但指针尚为空的竞态窗口
    g_engine.store(&engine, std::memory_order_release);
    install_signal_handlers();

    BIKE_LOG_INFO("gateway running: workers={} sq={} cq={}",
                  opt.workers, opt.sq_depth, opt.cq_depth);
    engine.run();   // 阻塞至优雅停机完成
    g_engine.store(nullptr, std::memory_order_release);
    BIKE_LOG_INFO("bike-server stopped");
    return 0;
}
