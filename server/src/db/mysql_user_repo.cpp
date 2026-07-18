#include "server/db/mysql_user_repo.hpp"
#include "server/logging.hpp"

#include <cstdio>
#include <cstring>

namespace bike::server {

MysqlUserRepo::MysqlUserRepo(std::shared_ptr<MysqlPool> pool) : pool_(pool) {}

std::optional<User> MysqlUserRepo::find_by_mobile(const std::string& mobile) {
    auto lease = pool_->acquire();
    char esc[64];
    mysql_real_escape_string(lease.get(), esc, mobile.data(),
                             static_cast<unsigned long>(mobile.size()));
    char sql[256];
    std::snprintf(sql, sizeof(sql),
                  "SELECT id, mobile FROM userinfo WHERE mobile='%s'", esc);
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        BIKE_LOG_ERROR("mysql query failed: {}", mysql_error(lease.get()));
        return std::nullopt;
    }
    MYSQL_RES* res = mysql_store_result(lease.get());
    if (!res) return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<User> u;
    if (row) {
        User t;
        t.id = std::atoi(row[0]);
        t.mobile = row[1] ? row[1] : "";
        u = t;
    }
    mysql_free_result(res);
    return u;
}

User MysqlUserRepo::find_or_create(const std::string& mobile) {
    if (auto existing = find_by_mobile(mobile)) return *existing;
    auto lease = pool_->acquire();
    char esc[64];
    mysql_real_escape_string(lease.get(), esc, mobile.data(),
                             static_cast<unsigned long>(mobile.size()));
    char sql[256];
    std::snprintf(sql, sizeof(sql),
                  "INSERT INTO userinfo (mobile) VALUES ('%s')", esc);
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        BIKE_LOG_ERROR("mysql insert failed: {}", mysql_error(lease.get()));
        return find_by_mobile(mobile).value_or(User{});
    }
    User u;
    u.id = static_cast<int>(mysql_insert_id(lease.get()));
    u.mobile = mobile;
    return u;
}

} // namespace bike::server
