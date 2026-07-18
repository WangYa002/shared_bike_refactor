#include "server/config.hpp"

#include <toml.hpp>
#include <stdexcept>

namespace bike::server {

namespace {
template <typename T>
T get_or(const toml::table& t, std::string_view key, T def) {
    auto node = t.get(key);
    if (!node) return def;
    auto v = node->value<T>();
    return v.value_or(def);
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
        cfg.server.threads = get_or<std::int64_t>(*s, "threads",cfg.server.threads);
    }
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
    return cfg;
}

} // namespace bike::server
