#pragma once

#include "server/repo/user_repo.hpp"
#include "server/db/mysql_pool.hpp"

#include <memory>

namespace bike::server {

class MysqlUserRepo : public IUserRepo {
public:
    explicit MysqlUserRepo(std::shared_ptr<MysqlPool> pool);
    std::optional<User> find_by_mobile(const std::string& mobile) override;
    User find_or_create(const std::string& mobile) override;
private:
    std::shared_ptr<MysqlPool> pool_;
};

} // namespace bike::server
