#pragma once

#include "server/repo/ride_repo.hpp"
#include "server/db/mysql_pool.hpp"

#include <memory>

namespace bike::server {

class MysqlRideRepo : public IRideRepo {
public:
    explicit MysqlRideRepo(std::shared_ptr<MysqlPool> pool);
    Ride create_with_points(const CreateRideInput& in) override;
    std::optional<Ride> find_by_no(const std::string& ride_no) override;
    std::vector<RidePoint> list_points(int ride_id) override;
    std::vector<Ride> list_by_user(int user_id, int limit) override;
private:
    std::shared_ptr<MysqlPool> pool_;
};

} // namespace bike::server
