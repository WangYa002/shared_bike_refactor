#include "server/config.hpp"

#include "bike/ipc/spsc_ring.hpp"   // [ipc] 槽数常量对齐校验

#include <toml.hpp>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace bike::server {

namespace {
template <typename T>
T get_or(const toml::table& t, std::string_view key, T def) {
    auto node = t.get(key);
    if (!node) return def;
    auto v = node->value<T>();
    return v.value_or(def);
}

[[noreturn]] void bad(const std::string& msg) {
    throw std::runtime_error("invalid config: " + msg);
}

// [uring] 段 clamp + 合法性校验: 低于下限报配置错误, 超过内核/工程上限收窄。
void validate_uring(Config::Uring& u) {
    if (u.sq_depth < 16) bad("[uring].sq_depth must be >= 16");
    if (u.sq_depth > 65536) u.sq_depth = 65536;

    if (u.cq_depth < 16) bad("[uring].cq_depth must be >= 16");
    if (u.cq_depth < u.sq_depth) u.cq_depth = u.sq_depth;   // 收窄: cq 不得小于 sq
    if (u.cq_depth > 131072) u.cq_depth = 131072;

    if (u.workers < 1) bad("[uring].workers must be >= 1");
    if (u.workers > 1024) u.workers = 1024;

    if (u.rx_buf_bytes < 4096) bad("[uring].rx_buf_bytes must be >= 4096");
    if (u.rx_buf_bytes > 16 * 1024 * 1024) u.rx_buf_bytes = 16 * 1024 * 1024;

    if (u.send_backlog_limit_bytes < 1) bad("[uring].send_backlog_limit_bytes must be >= 1");

    if (u.idle_timeout_ms < 0) bad("[uring].idle_timeout_ms must be >= 0 (0 = disable sweep)");
    if (u.idle_timeout_ms > 3600000) u.idle_timeout_ms = 3600000;

    if (u.accept_backlog < 1) bad("[uring].accept_backlog must be >= 1");
    if (u.accept_backlog > 65535) u.accept_backlog = 65535;
}

// [ipc] 段校验(模块三): mode 枚举 + 槽数与编译期常量对齐 + 参数区间。
void validate_ipc(Config::Ipc& ipc) {
    if (ipc.mode != "ring" && ipc.mode != "inprocess")
        bad("[ipc].mode must be \"ring\" or \"inprocess\"");
    if (ipc.shm_prefix.empty()) bad("[ipc].shm_prefix must be non-empty");
    if (ipc.instance < 0 || ipc.instance > 99) bad("[ipc].instance must be in [0, 99]");
    if (ipc.open_timeout_ms < 100) bad("[ipc].open_timeout_ms must be >= 100");
    if (ipc.peer_timeout_ms < 100) bad("[ipc].peer_timeout_ms must be >= 100");
    if (ipc.spin_tries < 0 || ipc.spin_tries > 10000)
        bad("[ipc].spin_tries must be in [0, 10000]");
    if (ipc.dispatch_workers < 1 || ipc.dispatch_workers > 1024)
        bad("[ipc].dispatch_workers must be in [1, 1024]");
    // 槽数为 v1 编译期常量: 配置不一致即启动报错, 防止双进程布局错位。
    if (ipc.req_ring_slots != static_cast<int>(bike::ipc::ReqRing::kSlotCount))
        bad("[ipc].req_ring_slots must equal compiled ReqRing::kSlotCount (" +
            std::to_string(bike::ipc::ReqRing::kSlotCount) + ")");
    if (ipc.rsp_ring_slots != static_cast<int>(bike::ipc::RspRing::kSlotCount))
        bad("[ipc].rsp_ring_slots must equal compiled RspRing::kSlotCount (" +
            std::to_string(bike::ipc::RspRing::kSlotCount) + ")");
}
} // namespace

Config load_config(const std::string& path) {
    Config cfg;
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("config parse failed: ") + e.what());
    }

    if (auto* s = root["server"].as_table()) {
        cfg.server.listen  = get_or<std::string>(*s, "listen",  cfg.server.listen);
        cfg.server.port    = get_or<std::int64_t>(*s, "port",   cfg.server.port);
        // 弃用键检测: threads 已由 [uring].workers 取代, 残留键只告警不生效
        if (s->contains("threads")) {
            std::fprintf(stderr,
                "[config WARN] [server].threads is deprecated and ignored; "
                "use [uring].workers (or BIKE_GATEWAY_WORKERS) instead\n");
        }
    }
    if (cfg.server.port < 1 || cfg.server.port > 65535)
        bad("[server].port must be in [1, 65535]");
    if (auto* m = root["mysql"].as_table()) {
        cfg.mysql.host      = get_or<std::string>(*m, "host",      cfg.mysql.host);
        cfg.mysql.port      = get_or<std::int64_t>(*m, "port",     cfg.mysql.port);
        cfg.mysql.user      = get_or<std::string>(*m, "user",      cfg.mysql.user);
        cfg.mysql.password  = get_or<std::string>(*m, "password",  cfg.mysql.password);
        cfg.mysql.database  = get_or<std::string>(*m, "database",  cfg.mysql.database);
        cfg.mysql.pool_size = get_or<std::int64_t>(*m, "pool_size",cfg.mysql.pool_size);
    }
    if (auto* r = root["redis"].as_table()) {
        cfg.redis.host      = get_or<std::string>(*r, "host",      cfg.redis.host);
        cfg.redis.port      = get_or<std::int64_t>(*r, "port",     cfg.redis.port);
        cfg.redis.pool_size = get_or<std::int64_t>(*r, "pool_size",cfg.redis.pool_size);
    }
    if (auto* l = root["log"].as_table()) {
        cfg.log.level = get_or<std::string>(*l, "level", cfg.log.level);
        cfg.log.file  = get_or<std::string>(*l, "file",  cfg.log.file);
    }
    if (auto* u = root["uring"].as_table()) {
        cfg.uring.sq_depth                   = get_or<std::int64_t>(*u, "sq_depth",                   cfg.uring.sq_depth);
        cfg.uring.cq_depth                   = get_or<std::int64_t>(*u, "cq_depth",                   cfg.uring.cq_depth);
        cfg.uring.workers                    = get_or<std::int64_t>(*u, "workers",                    cfg.uring.workers);
        cfg.uring.rx_buf_bytes               = get_or<std::int64_t>(*u, "rx_buf_bytes",               cfg.uring.rx_buf_bytes);
        cfg.uring.send_backlog_limit_bytes   = get_or<std::int64_t>(*u, "send_backlog_limit_bytes",   cfg.uring.send_backlog_limit_bytes);
        cfg.uring.idle_timeout_ms            = get_or<std::int64_t>(*u, "idle_timeout_ms",            cfg.uring.idle_timeout_ms);
        cfg.uring.accept_backlog             = get_or<std::int64_t>(*u, "accept_backlog",             cfg.uring.accept_backlog);
    }
    validate_uring(cfg.uring);
    if (auto* i = root["ipc"].as_table()) {
        cfg.ipc.mode             = get_or<std::string>(*i, "mode",             cfg.ipc.mode);
        cfg.ipc.shm_root         = get_or<std::string>(*i, "shm_root",         cfg.ipc.shm_root);
        cfg.ipc.shm_prefix       = get_or<std::string>(*i, "shm_prefix",       cfg.ipc.shm_prefix);
        cfg.ipc.instance         = get_or<std::int64_t>(*i, "instance",         cfg.ipc.instance);
        cfg.ipc.open_timeout_ms  = get_or<std::int64_t>(*i, "open_timeout_ms",  cfg.ipc.open_timeout_ms);
        cfg.ipc.peer_timeout_ms  = get_or<std::int64_t>(*i, "peer_timeout_ms",  cfg.ipc.peer_timeout_ms);
        cfg.ipc.spin_tries       = get_or<std::int64_t>(*i, "spin_tries",       cfg.ipc.spin_tries);
        cfg.ipc.dispatch_workers = get_or<std::int64_t>(*i, "dispatch_workers", cfg.ipc.dispatch_workers);
        cfg.ipc.req_ring_slots   = get_or<std::int64_t>(*i, "req_ring_slots",   cfg.ipc.req_ring_slots);
        cfg.ipc.rsp_ring_slots   = get_or<std::int64_t>(*i, "rsp_ring_slots",   cfg.ipc.rsp_ring_slots);
    }
    validate_ipc(cfg.ipc);
    return cfg;
}

} // namespace bike::server
