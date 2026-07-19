// server/include/server/ride_session_store.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace bike::server {

struct RideSession {
    std::string ride_no;
    int         user_id{0};
    int         bike_id{0};
    double      start_lat{0};
    double      start_lng{0};
    long long   start_ts{0};      // unix 秒
    double      last_lat{0};
    double      last_lng{0};
    int         last_seq{0};
};

class RideSessionStore {
public:
    void create(RideSession s) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        sessions_[s.ride_no] = std::move(s);
    }
    std::optional<RideSession> find(const std::string& ride_no) const {
        std::shared_lock<std::shared_mutex> lk(mu_);
        auto it = sessions_.find(ride_no);
        if (it == sessions_.end()) return std::nullopt;
        return it->second;
    }
    bool update_pos(const std::string& ride_no, double lat, double lng, int seq) {
        std::shared_lock<std::shared_mutex> lk(mu_);
        auto it = sessions_.find(ride_no);
        if (it == sessions_.end()) return false;
        it->second.last_lat = lat;
        it->second.last_lng = lng;
        it->second.last_seq = seq;
        return true;
    }
    bool remove(const std::string& ride_no) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        return sessions_.erase(ride_no) > 0;
    }
private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, RideSession> sessions_;
};

} // namespace bike::server
