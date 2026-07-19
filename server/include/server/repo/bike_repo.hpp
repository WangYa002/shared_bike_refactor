// server/include/server/repo/bike_repo.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bike::server {

enum class BikeStatus : int { Idle = 0, Rented = 1, Damaged = 2 };

struct Bike {
    int id{0};
    std::string bike_no;
    double lat{0};
    double lng{0};
    BikeStatus status{BikeStatus::Idle};
};

class IBikeRepo {
public:
    virtual ~IBikeRepo() = default;
    // FOR UPDATE 语义:在事务内对 bike_no 加行锁
    virtual std::optional<Bike> get_for_update(const std::string& bike_no) = 0;
    virtual bool update_status(int bike_id, BikeStatus status) = 0;
    virtual bool update_location(int bike_id, double lat, double lng) = 0;
    // bounding box 查询
    virtual std::vector<Bike> list_in_bounds(double lat_min, double lat_max,
                                             double lng_min, double lng_max) = 0;
};

} // namespace bike::server
