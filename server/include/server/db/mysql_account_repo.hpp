#pragma once

#include "server/repo/account_repo.hpp"
#include "server/db/mysql_pool.hpp"

#include <memory>

namespace bike::server {

class MysqlAccountRepo : public IAccountRepo {
public:
    explicit MysqlAccountRepo(std::shared_ptr<MysqlPool> pool);
    int get_balance(int user_id) override;
    int add_balance(int user_id, RecordType type, int amount) override;
    std::vector<AccountRecord> list_records(int user_id, int limit) override;
private:
    std::shared_ptr<MysqlPool> pool_;
};

} // namespace bike::server
