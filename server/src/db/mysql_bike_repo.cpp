#include "server/db/mysql_bike_repo.hpp"
#include "server/logging.hpp"

#include <cstdio>
#include <cstring>

namespace bike::server {

MysqlBikeRepo::MysqlBikeRepo(std::shared_ptr<MysqlPool> pool) : pool_(pool) {}

namespace {
Bike row_to_bike(MYSQL_ROW row, const unsigned long* lengths) {
    Bike b;
    b.id      = row[0] ? std::atoi(row[0]) : 0;
    b.bike_no = row[1] ? std::string(row[1], lengths[1]) : "";
    // lat / lng stored as DECIMAL(10,7) — read as string, parse to double
    b.lat     = row[2] ? std::atof(row[2]) : 0.0;
    b.lng     = row[3] ? std::atof(row[3]) : 0.0;
    b.status  = static_cast<BikeStatus>(row[4] ? std::atoi(row[4]) : 0);
    return b;
}
} // namespace

std::optional<Bike> MysqlBikeRepo::get_for_update(const std::string& bike_no) {
    auto lease = pool_->acquire();
    char esc[64];
    unsigned long n = mysql_real_escape_string(lease.get(), esc,
        bike_no.data(), static_cast<unsigned long>(bike_no.size()));
    char sql[256];
    std::snprintf(sql, sizeof(sql),
        "SELECT id, bike_no, lat, lng, status FROM bike WHERE bike_no='%.*s' FOR UPDATE",
        static_cast<int>(n), esc);
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        BIKE_LOG_ERROR("mysql get_for_update failed: {}", mysql_error(lease.get()));
        return std::nullopt;
    }
    MYSQL_RES* res = mysql_store_result(lease.get());
    if (!res) return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<Bike> out;
    if (row) {
        unsigned long* lengths = mysql_fetch_lengths(res);
        out = row_to_bike(row, lengths);
    }
    mysql_free_result(res);
    return out;
}

bool MysqlBikeRepo::update_status(int bike_id, BikeStatus s) {
    auto lease = pool_->acquire();
    char sql[128];
    std::snprintf(sql, sizeof(sql),
        "UPDATE bike SET status=%d WHERE id=%d", static_cast<int>(s), bike_id);
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        BIKE_LOG_ERROR("mysql update_status failed: {}", mysql_error(lease.get()));
        return false;
    }
    return mysql_affected_rows(lease.get()) > 0;
}

bool MysqlBikeRepo::update_location(int bike_id, double lat, double lng) {
    auto lease = pool_->acquire();
    char sql[160];
    std::snprintf(sql, sizeof(sql),
        "UPDATE bike SET lat=%.7f, lng=%.7f WHERE id=%d", lat, lng, bike_id);
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        BIKE_LOG_ERROR("mysql update_location failed: {}", mysql_error(lease.get()));
        return false;
    }
    return mysql_affected_rows(lease.get()) > 0;
}

std::vector<Bike> MysqlBikeRepo::list_in_bounds(double la_min, double la_max,
                                                double lo_min, double lo_max) {
    auto lease = pool_->acquire();
    char sql[256];
    std::snprintf(sql, sizeof(sql),
        "SELECT id, bike_no, lat, lng, status FROM bike "
        "WHERE status IN (0,2) AND lat BETWEEN %.7f AND %.7f AND lng BETWEEN %.7f AND %.7f",
        la_min, la_max, lo_min, lo_max);
    std::vector<Bike> out;
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        BIKE_LOG_ERROR("mysql list_in_bounds failed: {}", mysql_error(lease.get()));
        return out;
    }
    MYSQL_RES* res = mysql_store_result(lease.get());
    if (!res) return out;
    unsigned int n_fields = mysql_num_fields(res);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        if (n_fields < 5) continue;
        unsigned long* lengths = mysql_fetch_lengths(res);
        out.push_back(row_to_bike(row, lengths));
    }
    mysql_free_result(res);
    return out;
}

std::optional<Bike> MysqlBikeRepo::insert(const Bike& b) {
    auto lease = pool_->acquire();
    char esc[80];
    unsigned long n = mysql_real_escape_string(lease.get(), esc,
        b.bike_no.data(), static_cast<unsigned long>(b.bike_no.size()));
    char sql[256];
    std::snprintf(sql, sizeof(sql),
        "INSERT INTO bike (bike_no, lat, lng, status) VALUES ('%.*s', %.7f, %.7f, %d)",
        static_cast<int>(n), esc, b.lat, b.lng, static_cast<int>(b.status));
    if (mysql_real_query(lease.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        // bike_no UNIQUE 冲突(并发投放同号)或其它错误 → 返回 nullopt,
        // 调用方换新号重试;冲突属预期内,不作为致命错误。
        BIKE_LOG_WARN("mysql bike insert failed: {}", mysql_error(lease.get()));
        return std::nullopt;
    }
    Bike out = b;
    out.id = static_cast<int>(mysql_insert_id(lease.get()));
    return out;
}

} // namespace bike::server
