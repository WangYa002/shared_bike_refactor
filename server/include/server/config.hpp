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

    // mmap IPC 环 (模块三)。mode="ring" 时 Gateway/Dispatch 双进程 + SPSC 环;
    // mode="inprocess" 回退进程内 InProcessRouterSink 单进程模式。
    // instance 可用 BIKE_INSTANCE 环境变量覆盖(compose 双实例隔离)。
    struct Ipc {
        std::string mode{"ring"};       // "ring" | "inprocess"
        std::string shm_root{"/dev/shm"};  // FIFO 所在目录(shm 文件由 shm_open 固定落 /dev/shm)
        std::string shm_prefix{"bike"};    // shm 文件名: {prefix}{instance}_req / _rsp(shm_open 无后缀, 固定落 /dev/shm)
        int instance{0};
        int open_timeout_ms{10000};     // Gateway 等待 Dispatch 建环的上限
        int peer_timeout_ms{5000};      // 对端心跳超时(判死)
        int spin_tries{64};             // 读方睡眠前自旋次数
        int dispatch_workers{8};        // Dispatch 业务线程数 M
        int req_ring_slots{512};        // v1 必须 == ReqRing::kSlotCount(编译期常量)
        int rsp_ring_slots{256};        // v1 必须 == RspRing::kSlotCount
    } ipc;
};

// Throws std::runtime_error on parse error.
Config load_config(const std::string& path);

} // namespace bike::server
