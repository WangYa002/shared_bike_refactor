// server/include/server/repo/ride_repo.hpp
#pragma once

#include "server/repo/bike_repo.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bike::server {

struct RidePoint {
    int seq{0};
    double lat{0};
    double lng{0};
    int elapsed_sec{0};
};

struct Ride {
    int          id{0};
    std::string  ride_no;
    int          user_id{0};
    int          bike_id{0};
    long long    start_ts{0};    // unix 秒
    long long    end_ts{0};
    double       start_lat{0};
    double       start_lng{0};
    double       end_lat{0};
    double       end_lng{0};
    int          duration_sec{0};
    int          distance_m{0};
    int          amount_cent{0};
    int          status{0};
};

// 创建参数(避免直接构造完整 Ride,service 层只关心输入)
struct CreateRideInput {
    std::string ride_no;
    int         user_id;
    int         bike_id;
    long long   start_ts;
    long long   end_ts;
    double      start_lat;
    double      start_lng;
    double      end_lat;
    double      end_lng;
    int         duration_sec;
    int         distance_m;
    int         amount_cent;
    std::vector<RidePoint> points;
};

class IRideRepo {
public:
    virtual ~IRideRepo() = default;
    // 插入 ride + 关联 points,在同一事务内。返回插入后的 Ride(含 id)。
    virtual Ride create_with_points(const CreateRideInput& in) = 0;
    virtual std::optional<Ride> find_by_no(const std::string& ride_no) = 0;
    virtual std::vector<RidePoint> list_points(int ride_id) = 0;
    virtual std::vector<Ride> list_by_user(int user_id, int limit) = 0;
};

} // namespace bike::server
