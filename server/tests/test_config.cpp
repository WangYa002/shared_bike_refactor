#include "server/config.hpp"

#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>

using bike::server::Config;
using bike::server::load_config;

namespace {
std::string write_tmp(const std::string& content) {
    char buf[L_tmpnam];
    std::tmpnam(buf);
    std::string path = std::string(buf) + ".toml";
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}
} // namespace

TEST(Config, ParsesAllSections) {
    // 注: [server] 里故意残留弃用的 threads 键 —— 只应告警不应报错
    auto path = write_tmp(R"toml(
[server]
listen = "1.2.3.4"
port = 9000
threads = 2

[mysql]
host = "db.local"
port = 3307
user = "u"
password = "p"
database = "d"
pool_size = 4

[redis]
host = "r.local"
port = 6380
pool_size = 2

[log]
level = "debug"
file = "/tmp/x.log"

[uring]
sq_depth = 512
cq_depth = 1024
workers = 16
rx_buf_bytes = 32768
send_backlog_limit_bytes = 2097152
idle_timeout_ms = 30000
accept_backlog = 512
)toml");
    auto cfg = load_config(path);
    EXPECT_EQ(cfg.server.listen, "1.2.3.4");
    EXPECT_EQ(cfg.server.port, 9000);
    EXPECT_EQ(cfg.mysql.host, "db.local");
    EXPECT_EQ(cfg.mysql.pool_size, 4);
    EXPECT_EQ(cfg.redis.port, 6380);
    EXPECT_EQ(cfg.log.level, "debug");
    EXPECT_EQ(cfg.uring.sq_depth, 512);
    EXPECT_EQ(cfg.uring.cq_depth, 1024);
    EXPECT_EQ(cfg.uring.workers, 16);
    EXPECT_EQ(cfg.uring.rx_buf_bytes, 32768);
    EXPECT_EQ(cfg.uring.send_backlog_limit_bytes, 2097152);
    EXPECT_EQ(cfg.uring.idle_timeout_ms, 30000);
    EXPECT_EQ(cfg.uring.accept_backlog, 512);
    std::remove(path.c_str());
}

TEST(Config, MissingFileThrows) {
    EXPECT_THROW(load_config("/nonexistent/path.toml"), std::runtime_error);
}

TEST(Config, DefaultsWhenSectionMissing) {
    auto path = write_tmp(R"toml(
[server]
port = 7777
)toml");
    auto cfg = load_config(path);
    EXPECT_EQ(cfg.server.port, 7777);
    EXPECT_EQ(cfg.mysql.host, "127.0.0.1");  // default
    // [uring] 缺段时全部取默认值
    EXPECT_EQ(cfg.uring.sq_depth, 256);
    EXPECT_EQ(cfg.uring.cq_depth, 512);
    EXPECT_EQ(cfg.uring.workers, 8);
    EXPECT_EQ(cfg.uring.rx_buf_bytes, 65536);
    EXPECT_EQ(cfg.uring.send_backlog_limit_bytes, 1 << 20);
    EXPECT_EQ(cfg.uring.idle_timeout_ms, 60000);
    EXPECT_EQ(cfg.uring.accept_backlog, 1024);
    std::remove(path.c_str());
}

TEST(Config, InvalidUringValuesThrow) {
    // 低于下限 → 配置错误
    EXPECT_THROW(load_config(write_tmp("[uring]\nsq_depth = 8")), std::runtime_error);
    EXPECT_THROW(load_config(write_tmp("[uring]\nworkers = 0")), std::runtime_error);
    EXPECT_THROW(load_config(write_tmp("[uring]\nrx_buf_bytes = 1024")), std::runtime_error);
    EXPECT_THROW(load_config(write_tmp("[uring]\nidle_timeout_ms = -1")), std::runtime_error);
    EXPECT_THROW(load_config(write_tmp("[uring]\naccept_backlog = 0")), std::runtime_error);
    EXPECT_THROW(load_config(write_tmp("[server]\nport = 0")), std::runtime_error);
}

TEST(Config, UringValuesClamped) {
    // 超上限收窄; cq_depth < sq_depth 收窄为 sq_depth
    auto path = write_tmp(R"toml(
[uring]
sq_depth = 100000
cq_depth = 16
workers = 2000
accept_backlog = 99999
)toml");
    auto cfg = load_config(path);
    EXPECT_EQ(cfg.uring.sq_depth, 65536);
    EXPECT_EQ(cfg.uring.cq_depth, 65536);   // clamp 到 >= sq_depth
    EXPECT_EQ(cfg.uring.workers, 1024);
    EXPECT_EQ(cfg.uring.accept_backlog, 65535);
    std::remove(path.c_str());
}
