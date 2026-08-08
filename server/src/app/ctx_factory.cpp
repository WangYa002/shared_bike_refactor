// 模块三: 生产环境 Ctx 装配与 handler 注册实现。

#include "server/app/ctx_factory.hpp"

#include <bike/protocol.hpp>

#include <memory>

#include "server/handlers.hpp"
#include "server/db/mysql_pool.hpp"
#include "server/db/mysql_user_repo.hpp"
#include "server/db/mysql_account_repo.hpp"
#include "server/db/mysql_bike_repo.hpp"
#include "server/db/mysql_ride_repo.hpp"
#include "server/cache/redis_session_store.hpp"

namespace bike::server {

Ctx make_prod_ctx(const Config& cfg) {
    auto mysql_pool = std::make_shared<MysqlPool>(MysqlPool::Config{
        .host = cfg.mysql.host,
        .port = cfg.mysql.port,
        .user = cfg.mysql.user,
        .password = cfg.mysql.password,
        .database = cfg.mysql.database,
        .pool_size = cfg.mysql.pool_size,
    });

    return Ctx{
        .users         = std::make_shared<MysqlUserRepo>(mysql_pool),
        .accounts      = std::make_shared<MysqlAccountRepo>(mysql_pool),
        .sessions      = std::make_shared<RedisSessionStore>(cfg.redis.host, cfg.redis.port, cfg.redis.pool_size),
        .bikes         = std::make_shared<MysqlBikeRepo>(mysql_pool),
        .rides         = std::make_shared<MysqlRideRepo>(mysql_pool),
        .ride_sessions = std::make_shared<RideSessionStore>(),
    };
}

void register_all_handlers(Router& router) {
    router.register_handler(bike::event_id(bike::Event::MobileRequest), handlers::mobile_code);
    router.register_handler(bike::event_id(bike::Event::LoginRequest), handlers::login);
    router.register_handler(bike::event_id(bike::Event::RechargeRequest), handlers::recharge);
    router.register_handler(bike::event_id(bike::Event::AccountBalanceRequest), handlers::account_balance);
    router.register_handler(bike::event_id(bike::Event::ListAccountRecordsRequest), handlers::list_records);
    router.register_handler(bike::event_id(bike::Event::ListNearbyBikesRequest), handlers::list_nearby_bikes);
    router.register_handler(bike::event_id(bike::Event::ScanUnlockRequest), handlers::scan_unlock);
    router.register_handler(bike::event_id(bike::Event::RidePositionReport), handlers::position_report);
    router.register_handler(bike::event_id(bike::Event::EndRideRequest), handlers::end_ride);
    router.register_handler(bike::event_id(bike::Event::ReportDamageRequest), handlers::report_damage);
    router.register_handler(bike::event_id(bike::Event::GetRideDetailRequest), handlers::get_ride_detail);
    router.register_handler(bike::event_id(bike::Event::ListRidesRequest), handlers::list_rides);
}

} // namespace bike::server
