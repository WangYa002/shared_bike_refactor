#include "server/config.hpp"
#include "server/logging.hpp"
#include "server/server.hpp"
#include "server/router.hpp"
#include "server/handlers.hpp"
#include "server/repo/in_memory.hpp"
#include "server/db/mysql_pool.hpp"
#include "server/db/mysql_user_repo.hpp"
#include "server/db/mysql_account_repo.hpp"
#include "server/cache/redis_session_store.hpp"

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
        std::make_shared<MysqlUserRepo>(mysql_pool),
        std::make_shared<MysqlAccountRepo>(mysql_pool),
        std::make_shared<RedisSessionStore>(cfg.redis.host, cfg.redis.port, cfg.redis.pool_size),
    };

    Router router;
    router.register_handler(0x01, handlers::mobile_code);
    router.register_handler(0x03, handlers::login);
    router.register_handler(0x05, handlers::recharge);
    router.register_handler(0x07, handlers::account_balance);
    router.register_handler(0x09, handlers::list_records);

    asio::io_context ioc;
    Server server(ioc, cfg.server.listen, cfg.server.port, router, ctx);

    int n = std::max(1, cfg.server.threads);
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        pool.emplace_back([&ioc] { ioc.run(); });
    }
    BIKE_LOG_INFO("server running with {} worker threads", n);

    for (auto& t : pool) t.join();
    return 0;
}
