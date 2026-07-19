#include "server/db/mysql_ride_repo.hpp"
#include "server/logging.hpp"

#include <cstdio>
#include <cstring>

namespace bike::server {

MysqlRideRepo::MysqlRideRepo(std::shared_ptr<MysqlPool> pool) : pool_(pool) {}

namespace {
Ride row_to_ride(MYSQL_ROW row, const unsigned long* lengths) {
    // SELECT id, ride_no, user_id, bike_id,
    //        UNIX_TIMESTAMP(start_tm) AS start_ts, UNIX_TIMESTAMP(end_tm) AS end_ts,
    //        start_lat, start_lng, end_lat, end_lng,
    //        duration_sec, distance_m, amount_cent, status
    Ride r;
    r.id            = row[0] ? std::atoll(row[0]) : 0;
    r.ride_no       = row[1] ? std::string(row[1], lengths[1]) : "";
    r.user_id       = row[2] ? std::atoi(row[2]) : 0;
    r.bike_id       = row[3] ? std::atoi(row[3]) : 0;
    r.start_ts      = row[4] ? std::atoll(row[4]) : 0;
    r.end_ts        = row[5] ? std::atoll(row[5]) : 0;
    r.start_lat     = row[6] ? std::atof(row[6]) : 0.0;
    r.start_lng     = row[7] ? std::atof(row[7]) : 0.0;
    r.end_lat       = row[8] ? std::atof(row[8]) : 0.0;
    r.end_lng       = row[9] ? std::atof(row[9]) : 0.0;
    r.duration_sec  = row[10] ? std::atoi(row[10]) : 0;
    r.distance_m    = row[11] ? std::atoi(row[11]) : 0;
    r.amount_cent   = row[12] ? std::atoi(row[12]) : 0;
    r.status        = row[13] ? std::atoi(row[13]) : 0;
    return r;
}

bool run_sql(MYSQL* m, const char* sql) {
    if (mysql_real_query(m, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        BIKE_LOG_ERROR("mysql ride query failed: {} sql={}", mysql_error(m), sql);
        return false;
    }
    return true;
}
} // namespace

Ride MysqlRideRepo::create_with_points(const CreateRideInput& in) {
    auto lease = pool_->acquire();
    MYSQL* m = lease.get();

    char esc_no[64];
    unsigned long n_no = mysql_real_escape_string(m, esc_no,
        in.ride_no.data(), static_cast<unsigned long>(in.ride_no.size()));

    if (!run_sql(m, "START TRANSACTION")) {
        return Ride{};
    }

    // INSERT ride row
    char sql[512];
    std::snprintf(sql, sizeof(sql),
        "INSERT INTO ride(ride_no, user_id, bike_id, start_tm, end_tm, "
        "start_lat, start_lng, end_lat, end_lng, "
        "duration_sec, distance_m, amount_cent, status) "
        "VALUES('%.*s', %d, %d, FROM_UNIXTIME(%lld), FROM_UNIXTIME(%lld), "
        "%.7f, %.7f, %.7f, %.7f, %d, %d, %d, 0)",
        static_cast<int>(n_no), esc_no,
        in.user_id, in.bike_id,
        static_cast<long long>(in.start_ts), static_cast<long long>(in.end_ts),
        in.start_lat, in.start_lng, in.end_lat, in.end_lng,
        in.duration_sec, in.distance_m, in.amount_cent);

    if (!run_sql(m, sql)) {
        run_sql(m, "ROLLBACK");
        return Ride{};
    }

    long long ride_id = static_cast<long long>(mysql_insert_id(m));

    // INSERT each position point in same transaction
    for (const auto& p : in.points) {
        char psql[256];
        std::snprintf(psql, sizeof(psql),
            "INSERT INTO ride_position(ride_id, seq, lat, lng, elapsed_sec) "
            "VALUES(%lld, %d, %.7f, %.7f, %d)",
            ride_id, p.seq, p.lat, p.lng, p.elapsed_sec);
        if (!run_sql(m, psql)) {
            run_sql(m, "ROLLBACK");
            return Ride{};
        }
    }

    if (!run_sql(m, "COMMIT")) {
        run_sql(m, "ROLLBACK");
        return Ride{};
    }

    Ride r;
    r.id            = static_cast<int>(ride_id);
    r.ride_no       = in.ride_no;
    r.user_id       = in.user_id;
    r.bike_id       = in.bike_id;
    r.start_ts      = in.start_ts;
    r.end_ts        = in.end_ts;
    r.start_lat     = in.start_lat;
    r.start_lng     = in.start_lng;
    r.end_lat       = in.end_lat;
    r.end_lng       = in.end_lng;
    r.duration_sec  = in.duration_sec;
    r.distance_m    = in.distance_m;
    r.amount_cent   = in.amount_cent;
    r.status        = 0;
    return r;
}

std::optional<Ride> MysqlRideRepo::find_by_no(const std::string& no) {
    auto lease = pool_->acquire();
    MYSQL* m = lease.get();
    char esc[64];
    unsigned long n = mysql_real_escape_string(m, esc,
        no.data(), static_cast<unsigned long>(no.size()));
    char sql[256];
    std::snprintf(sql, sizeof(sql),
        "SELECT id, ride_no, user_id, bike_id, "
        "UNIX_TIMESTAMP(start_tm) AS start_ts, UNIX_TIMESTAMP(end_tm) AS end_ts, "
        "start_lat, start_lng, end_lat, end_lng, "
        "duration_sec, distance_m, amount_cent, status "
        "FROM ride WHERE ride_no='%.*s'",
        static_cast<int>(n), esc);
    if (!run_sql(m, sql)) return std::nullopt;
    MYSQL_RES* res = mysql_store_result(m);
    if (!res) return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<Ride> out;
    if (row) {
        unsigned long* lengths = mysql_fetch_lengths(res);
        out = row_to_ride(row, lengths);
    }
    mysql_free_result(res);
    return out;
}

std::vector<RidePoint> MysqlRideRepo::list_points(int ride_id) {
    auto lease = pool_->acquire();
    MYSQL* m = lease.get();
    char sql[160];
    std::snprintf(sql, sizeof(sql),
        "SELECT seq, lat, lng, elapsed_sec FROM ride_position "
        "WHERE ride_id=%d ORDER BY seq", ride_id);
    std::vector<RidePoint> out;
    if (!run_sql(m, sql)) return out;
    MYSQL_RES* res = mysql_store_result(m);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        RidePoint p;
        p.seq         = row[0] ? std::atoi(row[0]) : 0;
        p.lat         = row[1] ? std::atof(row[1]) : 0.0;
        p.lng         = row[2] ? std::atof(row[2]) : 0.0;
        p.elapsed_sec = row[3] ? std::atoi(row[3]) : 0;
        out.push_back(p);
    }
    mysql_free_result(res);
    return out;
}

std::vector<Ride> MysqlRideRepo::list_by_user(int uid, int limit) {
    auto lease = pool_->acquire();
    MYSQL* m = lease.get();
    char sql[256];
    std::snprintf(sql, sizeof(sql),
        "SELECT id, ride_no, user_id, bike_id, "
        "UNIX_TIMESTAMP(start_tm) AS start_ts, UNIX_TIMESTAMP(end_tm) AS end_ts, "
        "start_lat, start_lng, end_lat, end_lng, "
        "duration_sec, distance_m, amount_cent, status "
        "FROM ride WHERE user_id=%d ORDER BY start_tm DESC LIMIT %d",
        uid, limit);
    std::vector<Ride> out;
    if (!run_sql(m, sql)) return out;
    MYSQL_RES* res = mysql_store_result(m);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        unsigned long* lengths = mysql_fetch_lengths(res);
        out.push_back(row_to_ride(row, lengths));
    }
    mysql_free_result(res);
    return out;
}

} // namespace bike::server
