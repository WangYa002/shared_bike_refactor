#pragma once

#include <mysql/mysql.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace bike::server {

class MysqlPool {
public:
    struct Config {
        std::string host;
        int port{3306};
        std::string user;
        std::string password;
        std::string database;
        int pool_size{8};
    };

    explicit MysqlPool(const Config& cfg);
    ~MysqlPool();

    class Lease {
    public:
        Lease() = default;
        Lease(MysqlPool* p, MYSQL* m) : pool_(p), mysql_(m) {}
        ~Lease() { if (pool_ && mysql_) pool_->return_(mysql_); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& o) noexcept : pool_(o.pool_), mysql_(o.mysql_) {
            o.pool_ = nullptr; o.mysql_ = nullptr;
        }
        Lease& operator=(Lease&& o) noexcept {
            if (this != &o) {
                if (pool_ && mysql_) pool_->return_(mysql_);
                pool_ = o.pool_; mysql_ = o.mysql_;
                o.pool_ = nullptr; o.mysql_ = nullptr;
            }
            return *this;
        }
        MYSQL* get() const { return mysql_; }
        explicit operator bool() const { return mysql_ != nullptr; }
    private:
        MysqlPool* pool_{nullptr};
        MYSQL* mysql_{nullptr};
    };

    Lease acquire();
private:
    void return_(MYSQL* m);
    Config cfg_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<MYSQL*> pool_;
};

} // namespace bike::server
