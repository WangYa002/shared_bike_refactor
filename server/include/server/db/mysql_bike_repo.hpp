#pragma once

#include "server/repo/bike_repo.hpp"
#include "server/db/mysql_pool.hpp"

#include <memory>

namespace bike::server {

class MysqlBikeRepo : public IBikeRepo {
public:
    explicit MysqlBikeRepo(std::shared_ptr<MysqlPool> pool);
    std::optional<Bike> get_for_update(const std::string& bike_no) override;
    bool update_status(int bike_id, BikeStatus status) override;
    bool update_location(int bike_id, double lat, double lng) override;
    std::vector<Bike> list_in_bounds(double lat_min, double lat_max,
                                     double lng_min, double lng_max) override;
    std::optional<Bike> insert(const Bike& b) override;
private:
    std::shared_ptr<MysqlPool> pool_;
};

} // namespace bike::server
