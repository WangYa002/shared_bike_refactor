#pragma once

#include <cstdint>
#include <string>

namespace bike::server {

struct Config {
    struct Server {
        std::string listen{"0.0.0.0"};
        int port{8888};
        // 注: 旧 threads 字段已删除(网关改用 [uring].workers);
        // toml 里残留的 threads 键只触发 WARN, 不再生效。
    } server;

    struct Mysql {
        std::string host{"127.0.0.1"};
        int port{3306};
        std::string user{"bike"};
        std::string password;
        std::string database{"shared_bike"};
        int pool_size{8};
    } mysql;

    struct Redis {
        std::string host{"127.0.0.1"};
        int port{6379};
        int pool_size{4};
    } redis;

    struct Log {
        std::string level{"info"};
        std::string file{"/var/log/bike-server/server.log"};
    } log;

    // io_uring 网关 (模块二)。io 线程概念由单主线程 + uring.workers 取代;
    // 可用 BIKE_GATEWAY_WORKERS 环境变量覆盖。
    struct Uring {
        int sq_depth{256};
        int cq_depth{512};
        int workers{8};
        std::int64_t rx_buf_bytes{65536};
        std::int64_t send_backlog_limit_bytes{1 << 20};
        int idle_timeout_ms{60000};
        int accept_backlog{1024};
    } uring;
};

// Throws std::runtime_error on parse error.
Config load_config(const std::string& path);

} // namespace bike::server
