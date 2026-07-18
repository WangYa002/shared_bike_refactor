#pragma once

#include "server/repo/user_repo.hpp"
#include "server/repo/account_repo.hpp"
#include "server/repo/session_store.hpp"

#include <chrono>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <sstream>

namespace bike::server {

class InMemoryUserRepo : public IUserRepo {
public:
    std::optional<User> find_by_mobile(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = by_mobile_.find(m);
        if (it == by_mobile_.end()) return std::nullopt;
        return it->second;
    }
    User find_or_create(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = by_mobile_.find(m);
        if (it != by_mobile_.end()) return it->second;
        User u{.id = next_id_++, .mobile = m};
        by_mobile_[m] = u;
        return u;
    }
private:
    std::mutex mu_;
    std::map<std::string, User> by_mobile_;
    int next_id_{1};
};

class InMemoryAccountRepo : public IAccountRepo {
public:
    int get_balance(int uid) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = bal_.find(uid);
        return it == bal_.end() ? 0 : it->second;
    }
    int add_balance(int uid, RecordType type, int amount) override {
        std::lock_guard<std::mutex> lk(mu_);
        int& b = bal_[uid];
        b += amount;
        records_[uid].push_back(AccountRecord{
            .type = static_cast<int>(type),
            .amount = amount,
            .balance_after = b,
            .timestamp = std::chrono::seconds(std::time(nullptr)).count(),
        });
        return b;
    }
    std::vector<AccountRecord> list_records(int uid, int limit) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = records_.find(uid);
        if (it == records_.end()) return {};
        auto& v = it->second;
        if (v.size() <= static_cast<std::size_t>(limit)) return v;
        return std::vector<AccountRecord>(v.end() - limit, v.end());
    }
private:
    std::mutex mu_;
    std::map<int, int> bal_;
    std::map<int, std::vector<AccountRecord>> records_;
};

class InMemorySessionStore : public ISessionStore {
public:
    int set_code(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::uniform_int_distribution<int> dist(100000, 999999);
        int c = dist(rng_);
        codes_[m] = {c, std::chrono::steady_clock::now() + std::chrono::seconds(300)};
        return c;
    }
    std::optional<int> get_code(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = codes_.find(m);
        if (it == codes_.end()) return std::nullopt;
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            codes_.erase(it);
            return std::nullopt;
        }
        return it->second.code;
    }
    void delete_code(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        codes_.erase(m);
    }
    std::string create_session(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::uniform_int_distribution<unsigned long long> dist;
        std::ostringstream oss;
        oss << std::hex << dist(rng_) << dist(rng_);
        std::string token = oss.str();
        sessions_[token] = {m, std::chrono::steady_clock::now() + std::chrono::hours(7 * 24)};
        return token;
    }
    std::optional<std::string> lookup_session(const std::string& t) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = sessions_.find(t);
        if (it == sessions_.end()) return std::nullopt;
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            sessions_.erase(it);
            return std::nullopt;
        }
        return it->second.mobile;
    }
    void destroy_session(const std::string& t) override {
        std::lock_guard<std::mutex> lk(mu_);
        sessions_.erase(t);
    }
private:
    struct Entry {
        std::string mobile;
        std::chrono::steady_clock::time_point expiry;
    };
    struct CodeEntry {
        int code{0};
        std::chrono::steady_clock::time_point expiry;
    };
    std::mutex mu_;
    std::mt19937_64 rng_{std::random_device{}()};
    std::map<std::string, CodeEntry> codes_;
    std::map<std::string, Entry> sessions_;
};

} // namespace bike::server
