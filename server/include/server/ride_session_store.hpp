// server/include/server/ride_session_store.hpp
#pragma once

#include "server/repo/ride_repo.hpp" // RidePoint

#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
    // 骑行期间 0x15 位置上报累积的轨迹点(不含起点;终点在 end_ride 时补入)。
    std::vector<RidePoint> points;
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
    // 位置上报: seq 去重(seq <= 已见最大 seq 的重复/乱序包幂等忽略)。
    // 并发安全: 持 unique_lock(此前 shared_lock 下写会话成员存在数据竞争)。
    // elapsed_sec 缺省 0 以保持既有调用/测试兼容。
    bool update_pos(const std::string& ride_no, double lat, double lng, int seq,
                    int elapsed_sec = 0) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        auto it = sessions_.find(ride_no);
        if (it == sessions_.end()) return false;
        RideSession& s = it->second;
        // 首包可能 seq=0 或 seq=1;仅在已收到过有效包时才做去重判断。
        if ((s.last_seq != 0 || !s.points.empty()) && seq <= s.last_seq) return true;
        s.last_lat = lat;
        s.last_lng = lng;
        s.last_seq = seq;
        s.points.push_back(RidePoint{.seq = seq, .lat = lat, .lng = lng,
                                     .elapsed_sec = elapsed_sec});
        // 上限保护:超过 kMaxPoints(约 1 小时@1Hz)时丢弃最旧的点,
        // 优先保留靠近终点的轨迹以保证回放观感。
        if (s.points.size() > kMaxPoints) s.points.erase(s.points.begin());
        return true;
    }
    bool remove(const std::string& ride_no) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        return sessions_.erase(ride_no) > 0;
    }
private:
    static constexpr std::size_t kMaxPoints = 3600;
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, RideSession> sessions_;
};

} // namespace bike::server
