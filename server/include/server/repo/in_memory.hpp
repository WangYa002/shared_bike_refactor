#pragma once

#include "server/repo/bike_repo.hpp"
#include "server/repo/user_repo.hpp"
#include "server/repo/account_repo.hpp"
#include "server/repo/session_store.hpp"
#include "server/repo/ride_repo.hpp"

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
        int cur = bal_.count(uid) ? bal_[uid] : 0;
        if (amount < 0 && cur + amount < 0) return -1;   // 余额不足,不修改状态
        int& b = bal_[uid];
        b = cur + amount;
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
        std::size_t n = std::min(static_cast<std::size_t>(limit), v.size());
        // v is in insertion order (oldest first); contract requires newest first,
        // so take the last n elements and reverse them.
        return std::vector<AccountRecord>(v.rbegin(), v.rbegin() + n);
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

class InMemoryBikeRepo : public IBikeRepo {
public:
    std::optional<Bike> get_for_update(const std::string& no) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = bikes_.find(no);
        if (it == bikes_.end()) return std::nullopt;
        return it->second;
    }
    bool update_status(int id, BikeStatus s) override {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [_, b] : bikes_) {
            if (b.id == id) { b.status = s; return true; }
        }
        return false;
    }
    bool update_location(int id, double lat, double lng) override {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [_, b] : bikes_) {
            if (b.id == id) { b.lat = lat; b.lng = lng; return true; }
        }
        return false;
    }
    std::vector<Bike> list_in_bounds(double la_min, double la_max,
                                     double lo_min, double lo_max) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<Bike> out;
        for (auto& [_, b] : bikes_) {
            if (b.lat >= la_min && b.lat <= la_max &&
                b.lng >= lo_min && b.lng <= lo_max) {
                out.push_back(b);
            }
        }
        return out;
    }
    // 测试辅助
    void seed(Bike b) {
        std::lock_guard<std::mutex> lk(mu_);
        bikes_[b.bike_no] = b;
    }
    std::optional<Bike> insert(const Bike& b) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (bikes_.count(b.bike_no)) return std::nullopt;  // 与 UNIQUE 约束同语义
        Bike nb = b;
        nb.id = next_id_++;
        bikes_[nb.bike_no] = nb;
        return nb;
    }
private:
    std::mutex mu_;
    std::map<std::string, Bike> bikes_;
    int next_id_{1000000};   // 高位起步, 避免与 seed 数据的小 id 冲突
};

class InMemoryRideRepo : public IRideRepo {
public:
    Ride create_with_points(const CreateRideInput& in) override {
        std::lock_guard<std::mutex> lk(mu_);
        Ride r;
        r.id = next_id_++;
        r.ride_no = in.ride_no;
        r.user_id = in.user_id;
        r.bike_id = in.bike_id;
        r.start_ts = in.start_ts;
        r.end_ts = in.end_ts;
        r.start_lat = in.start_lat;
        r.start_lng = in.start_lng;
        r.end_lat = in.end_lat;
        r.end_lng = in.end_lng;
        r.duration_sec = in.duration_sec;
        r.distance_m = in.distance_m;
        r.amount_cent = in.amount_cent;
        r.status = 0;
        rides_[r.id] = r;
        points_[r.id] = in.points;
        by_no_[r.ride_no] = r.id;
        return r;
    }
    std::optional<Ride> find_by_no(const std::string& no) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = by_no_.find(no);
        if (it == by_no_.end()) return std::nullopt;
        return rides_[it->second];
    }
    std::vector<RidePoint> list_points(int ride_id) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = points_.find(ride_id);
        if (it == points_.end()) return {};
        return it->second;
    }
    std::vector<Ride> list_by_user(int uid, int limit) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<Ride> out;
        for (auto& [_, r] : rides_) {
            if (r.user_id == uid) out.push_back(r);
            if ((int)out.size() >= limit) break;
        }
        return out;
    }
private:
    std::mutex mu_;
    std::map<int, Ride> rides_;
    std::map<int, std::vector<RidePoint>> points_;
    std::map<std::string, int> by_no_;
    int next_id_{1};
};

} // namespace bike::server
