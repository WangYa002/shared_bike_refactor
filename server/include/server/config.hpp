#pragma once

#include <string>

namespace bike::server {

struct Config {
    struct Server {
        std::string listen{"0.0.0.0"};
        int port{8888};
        int threads{4};
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
};

// Throws std::runtime_error on parse error.
Config load_config(const std::string& path);

} // namespace bike::server
