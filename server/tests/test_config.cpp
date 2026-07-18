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
)toml");
    auto cfg = load_config(path);
    EXPECT_EQ(cfg.server.listen, "1.2.3.4");
    EXPECT_EQ(cfg.server.port, 9000);
    EXPECT_EQ(cfg.server.threads, 2);
    EXPECT_EQ(cfg.mysql.host, "db.local");
    EXPECT_EQ(cfg.mysql.pool_size, 4);
    EXPECT_EQ(cfg.redis.port, 6380);
    EXPECT_EQ(cfg.log.level, "debug");
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
    EXPECT_EQ(cfg.server.threads, 4);   // default
    EXPECT_EQ(cfg.mysql.host, "127.0.0.1");  // default
    std::remove(path.c_str());
}
