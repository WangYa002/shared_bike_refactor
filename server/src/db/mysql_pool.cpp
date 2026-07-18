#include "server/db/mysql_pool.hpp"
#include "server/logging.hpp"

#include <stdexcept>

namespace bike::server {

MysqlPool::MysqlPool(const Config& cfg) : cfg_(cfg) {
    for (int i = 0; i < cfg.pool_size; ++i) {
        MYSQL* m = mysql_init(nullptr);
        if (!m) throw std::runtime_error("mysql_init failed");
        if (!mysql_real_connect(m, cfg.host.c_str(), cfg.user.c_str(),
                                cfg.password.c_str(), cfg.database.c_str(),
                                cfg.port, nullptr, CLIENT_MULTI_STATEMENTS)) {
            std::string err = mysql_error(m);
            mysql_close(m);
            throw std::runtime_error("mysql_real_connect: " + err);
        }
        bool reconnect = true;
        mysql_options(m, MYSQL_OPT_RECONNECT, &reconnect);
        pool_.push(m);
    }
    BIKE_LOG_INFO("mysql pool ready ({} conns to {}:{})",
                  cfg.pool_size, cfg.host, cfg.port);
}

MysqlPool::~MysqlPool() {
    std::lock_guard<std::mutex> lk(mu_);
    while (!pool_.empty()) {
        mysql_close(pool_.front());
        pool_.pop();
    }
}

MysqlPool::Lease MysqlPool::acquire() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this]{ return !pool_.empty(); });
    MYSQL* m = pool_.front();
    pool_.pop();
    return Lease(this, m);
}

void MysqlPool::return_(MYSQL* m) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        pool_.push(m);
    }
    cv_.notify_one();
}

} // namespace bike::server
