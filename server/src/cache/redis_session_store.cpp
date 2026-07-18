#include "server/cache/redis_session_store.hpp"
#include "server/logging.hpp"

#include <random>
#include <sstream>

namespace bike::server {

struct RedisSessionStore::Impl {
    std::mutex mu;
    std::vector<redisContext*> pool;
    std::mt19937_64 rng{std::random_device{}()};
    std::string host;
    int port{6379};

    redisContext* acquire() {
        std::lock_guard<std::mutex> lk(mu);
        if (pool.empty()) {
            struct timeval tv{.tv_sec = 2, .tv_usec = 0};
            auto* c = redisConnectWithTimeout(host.c_str(), port, tv);
            if (c && c->err) {
                BIKE_LOG_ERROR("redis connect failed: {}", c->errstr);
                redisFree(c);
                return nullptr;
            }
            return c;
        }
        auto* c = pool.back();
        pool.pop_back();
        return c;
    }
    void release(redisContext* c) {
        if (!c) return;
        std::lock_guard<std::mutex> lk(mu);
        pool.push_back(c);
    }
};

RedisSessionStore::RedisSessionStore(std::string host, int port, int pool_size)
    : impl_(std::make_unique<Impl>()) {
    impl_->host = std::move(host);
    impl_->port = port;
    for (int i = 0; i < pool_size; ++i) {
        struct timeval tv{.tv_sec = 2, .tv_usec = 0};
        auto* c = redisConnectWithTimeout(impl_->host.c_str(), port, tv);
        if (!c || c->err) {
            BIKE_LOG_ERROR("redis connect failed: {}", c ? c->errstr : "alloc");
            if (c) redisFree(c);
            continue;
        }
        impl_->pool.push_back(c);
    }
    BIKE_LOG_INFO("redis pool ready ({} conns to {}:{})",
                  static_cast<int>(impl_->pool.size()), impl_->host, impl_->port);
}

RedisSessionStore::~RedisSessionStore() {
    for (auto* c : impl_->pool) redisFree(c);
}

int RedisSessionStore::set_code(const std::string& m) {
    auto* c = impl_->acquire();
    if (!c) return 0;
    std::uniform_int_distribution<int> dist(100000, 999999);
    int code = dist(impl_->rng);
    std::string key = "code:" + m;
    auto* r = static_cast<redisReply*>(redisCommand(c, "SET %s %d EX 300", key.c_str(), code));
    if (!r) {
        impl_->release(c);
        return code;
    }
    freeReplyObject(r);
    impl_->release(c);
    return code;
}

std::optional<int> RedisSessionStore::get_code(const std::string& m) {
    auto* c = impl_->acquire();
    if (!c) return std::nullopt;
    std::string key = "code:" + m;
    auto* r = static_cast<redisReply*>(redisCommand(c, "GET %s", key.c_str()));
    std::optional<int> out;
    if (r && r->type == REDIS_REPLY_STRING) out = std::atoi(r->str);
    if (r) freeReplyObject(r);
    impl_->release(c);
    return out;
}

void RedisSessionStore::delete_code(const std::string& m) {
    auto* c = impl_->acquire();
    if (!c) return;
    std::string key = "code:" + m;
    auto* r = static_cast<redisReply*>(redisCommand(c, "DEL %s", key.c_str()));
    if (r) freeReplyObject(r);
    impl_->release(c);
}

std::string RedisSessionStore::create_session(const std::string& m) {
    auto* c = impl_->acquire();
    if (!c) return {};
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream oss;
    oss << std::hex << dist(impl_->rng) << dist(impl_->rng);
    std::string token = oss.str();
    std::string key = "session:" + token;
    auto* r = static_cast<redisReply*>(redisCommand(c, "SET %s %s EX 604800",
        key.c_str(), m.c_str()));
    if (r) freeReplyObject(r);
    impl_->release(c);
    return token;
}

std::optional<std::string> RedisSessionStore::lookup_session(const std::string& t) {
    auto* c = impl_->acquire();
    if (!c) return std::nullopt;
    std::string key = "session:" + t;
    auto* r = static_cast<redisReply*>(redisCommand(c, "GET %s", key.c_str()));
    std::optional<std::string> out;
    if (r && r->type == REDIS_REPLY_STRING) out = std::string(r->str, r->len);
    if (r) freeReplyObject(r);
    impl_->release(c);
    return out;
}

void RedisSessionStore::destroy_session(const std::string& t) {
    auto* c = impl_->acquire();
    if (!c) return;
    std::string key = "session:" + t;
    auto* r = static_cast<redisReply*>(redisCommand(c, "DEL %s", key.c_str()));
    if (r) freeReplyObject(r);
    impl_->release(c);
}

} // namespace bike::server
