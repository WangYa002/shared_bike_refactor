#pragma once

#include "server/repo/user_repo.hpp"
#include <vector>

namespace bike::server {

enum class RecordType : int { Recharge = 1, Consume = 2 };

struct AccountRecord {
    int type{0};
    int amount{0};
    long long balance_after{0};
    long long timestamp{0};
};

class IAccountRepo {
public:
    virtual ~IAccountRepo() = default;
    virtual int get_balance(int user_id) = 0;
    virtual int add_balance(int user_id, RecordType type, int amount) = 0;
    virtual std::vector<AccountRecord> list_records(int user_id, int limit) = 0;
};

} // namespace bike::server
