#include "server/db/mysql_account_repo.hpp"
#include "server/logging.hpp"

#include <cstdio>
#include <cstring>

namespace bike::server {

MysqlAccountRepo::MysqlAccountRepo(std::shared_ptr<MysqlPool> pool) : pool_(pool) {}

int MysqlAccountRepo::get_balance(int uid) {
    auto lease = pool_->acquire();
    char sql[128];
    std::snprintf(sql, sizeof(sql),
                  "SELECT balance FROM account WHERE user_id=%d", uid);
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
        return 0;
    MYSQL_RES* res = mysql_store_result(lease.get());
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int bal = row ? std::atoi(row[0]) : 0;
    mysql_free_result(res);
    return bal;
}

int MysqlAccountRepo::add_balance(int uid, RecordType type, int amount) {
    auto lease = pool_->acquire();
    auto run = [&](const char* sql) {
        return mysql_real_query(lease.get(), sql,
                                static_cast<unsigned long>(std::strlen(sql))) == 0;
    };
    if (!run("START TRANSACTION")) {
        BIKE_LOG_ERROR("BEGIN failed: {}", mysql_error(lease.get()));
        return get_balance(uid);
    }

    char buf1[128], buf2[256], buf3[256];
    std::snprintf(buf1, sizeof(buf1),
        "INSERT IGNORE INTO account (user_id, balance) VALUES (%d, 0)", uid);
    std::snprintf(buf2, sizeof(buf2),
        "UPDATE account SET balance = balance + %d WHERE user_id = %d", amount, uid);
    std::snprintf(buf3, sizeof(buf3),
        "INSERT INTO account_record (user_id, type, amount, balance_after) "
        "SELECT %d, %d, %d, balance FROM account WHERE user_id = %d",
        uid, static_cast<int>(type), amount, uid);

    bool ok = run(buf1) && run(buf2) && run(buf3);
    if (!ok) {
        BIKE_LOG_ERROR("add_balance failed: {}", mysql_error(lease.get()));
        run("ROLLBACK");
        return get_balance(uid);
    }
    if (!run("COMMIT")) {
        run("ROLLBACK");
    }
    return get_balance(uid);
}

std::vector<AccountRecord> MysqlAccountRepo::list_records(int uid, int limit) {
    auto lease = pool_->acquire();
    char sql[256];
    std::snprintf(sql, sizeof(sql),
        "SELECT type, amount, balance_after, UNIX_TIMESTAMP(tm) "
        "FROM account_record WHERE user_id=%d ORDER BY tm DESC LIMIT %d",
        uid, limit);
    std::vector<AccountRecord> out;
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
        return out;
    MYSQL_RES* res = mysql_store_result(lease.get());
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        AccountRecord r;
        r.type = std::atoi(row[0]);
        r.amount = std::atoi(row[1]);
        r.balance_after = std::atoll(row[2]);
        r.timestamp = std::atoll(row[3]);
        out.push_back(r);
    }
    mysql_free_result(res);
    return out;
}

} // namespace bike::server
