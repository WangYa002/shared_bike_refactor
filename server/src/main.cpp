#include "server/config.hpp"
#include "server/logging.hpp"
#include "server/server.hpp"
#include "server/router.hpp"
#include "server/handlers.hpp"
#include "server/db/mysql_pool.hpp"
#include "server/db/mysql_user_repo.hpp"
#include "server/db/mysql_account_repo.hpp"
#include "server/db/mysql_bike_repo.hpp"
#include "server/db/mysql_ride_repo.hpp"
#include "server/cache/redis_session_store.hpp"
#include "server/util/thread_pool.hpp"

#include <asio.hpp>

#include <cstdio>
#include <thread>
#include <vector>

using namespace bike::server;

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
    BIKE_LOG_INFO("starting bike-server, listening {}:{}", cfg.server.listen, cfg.server.port);

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
    router.register_handler(0x01, handlers::mobile_code);
    router.register_handler(0x03, handlers::login);
    router.register_handler(0x05, handlers::recharge);
    router.register_handler(0x07, handlers::account_balance);
    router.register_handler(0x09, handlers::list_records);
    router.register_handler(0x11, handlers::list_nearby_bikes);
    router.register_handler(0x13, handlers::scan_unlock);
    router.register_handler(0x15, handlers::position_report);
    router.register_handler(0x17, handlers::end_ride);
    router.register_handler(0x19, handlers::report_damage);
    router.register_handler(0x1B, handlers::get_ride_detail);
    router.register_handler(0x1D, handlers::list_rides);

    // 业务线程池: 大小 = io worker 数 * 2 (dispatch 主要等 redis/mysql,
    // 不抢 CPU, 多开一些可以隐藏下游延迟). 可用 BIKE_BIZ_THREADS 环境变量覆盖,
    // 便于不重新编译就能调参压测.
    int io_threads = std::max(1, cfg.server.threads);
    int biz_threads = std::max(2, io_threads * 2);
    if (const char* env = std::getenv("BIKE_BIZ_THREADS")) {
        int v = std::atoi(env);
        if (v > 0) biz_threads = v;
    }
    ThreadPool biz_pool(static_cast<std::size_t>(biz_threads));

    asio::io_context ioc;
    Server server(ioc, cfg.server.listen, cfg.server.port, router, ctx, biz_pool);

    std::vector<std::thread> io_pool;
    io_pool.reserve(static_cast<std::size_t>(io_threads));
    for (int i = 0; i < io_threads; ++i) {
        io_pool.emplace_back([&ioc] { ioc.run(); });
    }
    BIKE_LOG_INFO("server running: io_workers={} biz_workers={}", io_threads, biz_threads);

    for (auto& t : io_pool) t.join();
    return 0;
}
