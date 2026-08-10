#include "server/handlers.hpp"
#include "server/repo/in_memory.hpp"
#include "server/ride_session_store.hpp"
#include "server/router.hpp"
#include "bike/protocol.hpp"

#include <bike.pb.h>
#include <gtest/gtest.h>

#include <ctime>
#include <set>
#include <string>

#include "bike/geo.hpp"

using namespace bike;
using namespace bike::server;

namespace {

struct Fixture {
    std::shared_ptr<InMemoryUserRepo>     users{std::make_shared<InMemoryUserRepo>()};
    std::shared_ptr<InMemoryAccountRepo>  accounts{std::make_shared<InMemoryAccountRepo>()};
    std::shared_ptr<InMemorySessionStore> sessions{std::make_shared<InMemorySessionStore>()};
    std::shared_ptr<InMemoryBikeRepo>     bikes{std::make_shared<InMemoryBikeRepo>()};
    std::shared_ptr<InMemoryRideRepo>     rides{std::make_shared<InMemoryRideRepo>()};
    std::shared_ptr<RideSessionStore>     ride_sessions{std::make_shared<RideSessionStore>()};
    Ctx ctx{};
    std::string token;

    Fixture() {
        ctx.users         = users;
        ctx.accounts      = accounts;
        ctx.sessions      = sessions;
        ctx.bikes         = bikes;
        ctx.rides         = rides;
        ctx.ride_sessions = ride_sessions;
        token = sessions->create_session("15600000010");
    }
};

tutorial::list_nearby_bikes_response parse_nearby(const std::vector<std::uint8_t>& bytes) {
    auto fr = decode(bytes.data(), bytes.size());
    tutorial::list_nearby_bikes_response r;
    if (fr) {
        r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    }
    return r;
}

} // namespace

TEST(ListNearbyBikes, ReturnsIdleAndDamagedInRadius) {
    Fixture f;
    // 种 5 辆(达投放阈值), 验证"不投放且 idle/damaged 都返回"的纯查询路径
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    f.bikes->seed({.id = 2, .bike_no = "BJ-002", .lat = 39.983, .lng = 116.315, .status = BikeStatus::Damaged});
    f.bikes->seed({.id = 3, .bike_no = "BJ-003", .lat = 40.000, .lng = 116.500, .status = BikeStatus::Idle});
    f.bikes->seed({.id = 4, .bike_no = "BJ-004", .lat = 39.9825, .lng = 116.3145, .status = BikeStatus::Idle});
    f.bikes->seed({.id = 5, .bike_no = "BJ-005", .lat = 39.9818, .lng = 116.3138, .status = BikeStatus::Damaged});
    f.bikes->seed({.id = 6, .bike_no = "BJ-006", .lat = 39.9822, .lng = 116.3142, .status = BikeStatus::Idle});

    tutorial::list_nearby_bikes_request req;
    req.set_session_token(f.token);
    req.set_lat(39.982);
    req.set_lng(116.314);
    req.set_radius_m(500);

    auto bytes = handlers::list_nearby_bikes(req.SerializeAsString(), f.ctx);
    auto rsp = parse_nearby(bytes);
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.bikes_size(), 5);
    // 充足区域不投放: 无 DY- 动态车。
    for (const auto& b : rsp.bikes())
        EXPECT_NE(b.bike_no().substr(0, 3), "DY-");
}

// 稀疏区域(0 辆)触发动态投放, 补足到目标数量, 全部 idle 且在半径内。
TEST(ListNearbyBikes, SpawnsDynamicBikesWhenSparse) {
    Fixture f;
    tutorial::list_nearby_bikes_request req;
    req.set_session_token(f.token);
    req.set_lat(31.2304);   // 无任何种子车的区域
    req.set_lng(121.4737);
    req.set_radius_m(500);

    auto rsp = parse_nearby(handlers::list_nearby_bikes(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    ASSERT_GE(rsp.bikes_size(), 10);   // 补到约 12 辆
    std::set<std::string> nos;
    for (const auto& b : rsp.bikes()) {
        EXPECT_EQ(b.status(), 0);                          // 投放车全部 idle
        EXPECT_EQ(b.bike_no().substr(0, 3), "DY-");        // 动态车号前缀
        EXPECT_LE(haversine_m(31.2304, 121.4737, b.lat(), b.lng()), 500.0);
        nos.insert(b.bike_no());
    }
    EXPECT_EQ(nos.size(), static_cast<size_t>(rsp.bikes_size()));  // 车号无重复
}

// 车号唯一性: 两个不同稀疏区域先后投放, 车号全局不重复;
// 且投放达标后同区域再请求不再增车(防抖收敛)。
TEST(ListNearbyBikes, SpawnedBikeNosUniqueAndNoOverSpawn) {
    Fixture f;
    auto query = [&](double lat, double lng) {
        tutorial::list_nearby_bikes_request req;
        req.set_session_token(f.token);
        req.set_lat(lat); req.set_lng(lng); req.set_radius_m(500);
        return parse_nearby(handlers::list_nearby_bikes(req.SerializeAsString(), f.ctx));
    };
    auto rsp1 = query(31.2304, 121.4737);
    auto rsp2 = query(23.1291, 113.2644);
    std::set<std::string> nos;
    for (const auto& b : rsp1.bikes()) nos.insert(b.bike_no());
    for (const auto& b : rsp2.bikes()) nos.insert(b.bike_no());
    EXPECT_EQ(nos.size(),
              static_cast<size_t>(rsp1.bikes_size() + rsp2.bikes_size()));

    // 防抖: 首次投放后区域达标, 再次请求数量不变(不再增车)。
    auto rsp3 = query(31.2304, 121.4737);
    EXPECT_EQ(rsp3.bikes_size(), rsp1.bikes_size());
}

TEST(ListNearbyBikes, UnauthorizedIfBadToken) {
    Fixture f;
    tutorial::list_nearby_bikes_request req;
    req.set_session_token("invalid");
    req.set_lat(39.982); req.set_lng(116.314); req.set_radius_m(500);
    auto bytes = handlers::list_nearby_bikes(req.SerializeAsString(), f.ctx);
    auto rsp = parse_nearby(bytes);
    EXPECT_EQ(rsp.code(), 401);
}

TEST(ListNearbyBikes, InvalidLatRejected) {
    Fixture f;
    tutorial::list_nearby_bikes_request req;
    req.set_session_token(f.token);
    req.set_lat(99.0); req.set_lng(116.314); req.set_radius_m(500);
    auto bytes = handlers::list_nearby_bikes(req.SerializeAsString(), f.ctx);
    auto rsp = parse_nearby(bytes);
    EXPECT_EQ(rsp.code(), 404);
}

tutorial::scan_unlock_response parse_unlock(const std::vector<std::uint8_t>& bytes) {
    auto fr = decode(bytes.data(), bytes.size());
    tutorial::scan_unlock_response r;
    if (fr) {
        r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    }
    return r;
}

TEST(ScanUnlock, SuccessCreatesSessionAndRentsBike) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    // User id is 1 (first user via find_or_create in Fixture's token creation flow).
    f.accounts->add_balance(/*user_id=*/1, RecordType::Recharge, 1000);

    tutorial::scan_unlock_request req;
    req.set_session_token(f.token);
    req.set_bike_no("BJ-001");
    req.set_lat(39.982); req.set_lng(116.314);
    auto bytes = handlers::scan_unlock(req.SerializeAsString(), f.ctx);
    auto rsp = parse_unlock(bytes);
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_FALSE(rsp.ride_no().empty());
    EXPECT_GT(rsp.start_ts(), 0);

    auto bike = f.bikes->get_for_update("BJ-001");
    EXPECT_EQ(bike->status, BikeStatus::Rented);
    EXPECT_TRUE(f.ride_sessions->find(rsp.ride_no()).has_value());
}

TEST(ScanUnlock, DamagedBikeReturns409) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Damaged});
    tutorial::scan_unlock_request req;
    req.set_session_token(f.token);
    req.set_bike_no("BJ-001");
    req.set_lat(39.982); req.set_lng(116.314);
    auto rsp = parse_unlock(handlers::scan_unlock(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 409);
    auto bike = f.bikes->get_for_update("BJ-001");
    EXPECT_EQ(bike->status, BikeStatus::Damaged);
}

TEST(ScanUnlock, RentedBikeReturns408) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Rented});
    tutorial::scan_unlock_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-001");
    req.set_lat(39.982); req.set_lng(116.314);
    auto rsp = parse_unlock(handlers::scan_unlock(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 408);
}

TEST(ScanUnlock, InsufficientBalanceReturns406) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    tutorial::scan_unlock_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-001");
    req.set_lat(39.982); req.set_lng(116.314);
    auto rsp = parse_unlock(handlers::scan_unlock(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 406);
}

TEST(ScanUnlock, UnknownBikeReturns404) {
    Fixture f;
    tutorial::scan_unlock_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-NOSUCH");
    req.set_lat(39.982); req.set_lng(116.314);
    auto rsp = parse_unlock(handlers::scan_unlock(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 404);
}

TEST(PositionReport, UpdatesSessionPos) {
    Fixture f;
    f.ride_sessions->create({.ride_no = "R1", .user_id = 1, .bike_id = 1});
    tutorial::ride_position_report req;
    req.set_ride_no("R1"); req.set_seq(5);
    req.set_lat(39.985); req.set_lng(116.318); req.set_elapsed_sec(5);
    auto bytes = handlers::position_report(req.SerializeAsString(), f.ctx);
    EXPECT_TRUE(bytes.empty());
    auto s = f.ride_sessions->find("R1");
    EXPECT_DOUBLE_EQ(s->last_lat, 39.985);
    EXPECT_EQ(s->last_seq, 5);
    // 上报点应同时累积进轨迹(含 elapsed_sec)。
    ASSERT_EQ(s->points.size(), 1u);
    EXPECT_EQ(s->points[0].seq, 5);
    EXPECT_DOUBLE_EQ(s->points[0].lat, 39.985);
    EXPECT_EQ(s->points[0].elapsed_sec, 5);
}

TEST(PositionReport, UnknownRideIsSilent) {
    Fixture f;
    tutorial::ride_position_report req;
    req.set_ride_no("RNONE");
    auto bytes = handlers::position_report(req.SerializeAsString(), f.ctx);
    EXPECT_TRUE(bytes.empty());
}

tutorial::end_ride_response parse_end(const std::vector<std::uint8_t>& bytes) {
    auto fr = decode(bytes.data(), bytes.size());
    tutorial::end_ride_response r;
    if (fr) {
        r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    }
    return r;
}

TEST(EndRide, SuccessPathChargesAndArchives) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Rented});
    f.accounts->add_balance(1, RecordType::Recharge, 1000);
    // start_ts = 60 seconds ago so duration_sec ~= 60 → fee 100
    long long past = static_cast<long long>(std::time(nullptr)) - 60;
    f.ride_sessions->create({
        .ride_no = "R1", .user_id = 1, .bike_id = 1,
        .start_lat = 39.982, .start_lng = 116.314,
        .start_ts = past,
    });

    tutorial::end_ride_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    req.set_end_lat(39.985); req.set_end_lng(116.318);
    auto rsp = parse_end(handlers::end_ride(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_GT(rsp.amount_cent(), 0);
    EXPECT_EQ(rsp.balance_after(), 1000 - rsp.amount_cent());

    auto bike = f.bikes->get_for_update("BJ-001");
    EXPECT_EQ(bike->status, BikeStatus::Idle);
    EXPECT_NEAR(bike->lat, 39.985, 0.0001);

    EXPECT_FALSE(f.ride_sessions->find("R1").has_value());

    auto ride = f.rides->find_by_no("R1");
    ASSERT_TRUE(ride.has_value());
    EXPECT_EQ(ride->amount_cent, rsp.amount_cent());
    // 无上报点时回退两点行为: 起点 + 终点。
    auto pts = f.rides->list_points(ride->id);
    ASSERT_EQ(pts.size(), 2u);
    EXPECT_DOUBLE_EQ(pts.front().lat, 39.982);
    EXPECT_DOUBLE_EQ(pts.back().lat, 39.985);
}

// 骑行期间上报的轨迹点应全部落库(起点+上报点+终点),
// 且里程为沿轨迹逐段累加(明显大于起终点直线距离)。
TEST(EndRide, PersistsAccumulatedTrajectoryAndSegmentedDistance) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Rented});
    f.accounts->add_balance(1, RecordType::Recharge, 1000);
    long long past = static_cast<long long>(std::time(nullptr)) - 60;
    f.ride_sessions->create({
        .ride_no = "R1", .user_id = 1, .bike_id = 1,
        .start_lat = 39.982, .start_lng = 116.314,
        .start_ts = past,
    });
    // 骑行中上报 5 个点, 中间明显向北绕行(直线距离仅约 340m)。
    const double lats[5] = {39.986, 39.990, 39.986, 39.983, 39.982};
    const double lngs[5] = {116.315, 116.316, 116.317, 116.318, 116.317};
    for (int i = 1; i <= 5; ++i)
        f.ride_sessions->update_pos("R1", lats[i - 1], lngs[i - 1], i, i * 10);

    tutorial::end_ride_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    req.set_end_lat(39.982); req.set_end_lng(116.318);
    auto rsp = parse_end(handlers::end_ride(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);

    auto ride = f.rides->find_by_no("R1");
    ASSERT_TRUE(ride.has_value());
    auto pts = f.rides->list_points(ride->id);
    ASSERT_EQ(pts.size(), 7u);  // 起点 + 5 上报点 + 终点
    EXPECT_DOUBLE_EQ(pts.front().lat, 39.982);          // 起点
    EXPECT_EQ(pts.front().elapsed_sec, 0);
    EXPECT_DOUBLE_EQ(pts.back().lat, 39.982);           // 终点
    EXPECT_DOUBLE_EQ(pts.back().lng, 116.318);
    // 按 elapsed_sec 排序(回放时间轴有序)。
    for (size_t i = 1; i < pts.size(); ++i)
        EXPECT_GE(pts[i].elapsed_sec, pts[i - 1].elapsed_sec);
    // 逐段里程明显大于起终点直线距离(约 340m), 验证非直线算法。
    EXPECT_GT(ride->distance_m, 1000);
    EXPECT_EQ(rsp.distance_m(), ride->distance_m);
}

TEST(EndRide, IdempotentReturnsHistoryWithoutDoubleCharge) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    f.accounts->add_balance(1, RecordType::Recharge, 1000);
    f.rides->create_with_points({
        .ride_no = "R1", .user_id = 1, .bike_id = 1,
        .start_ts = 1000, .end_ts = 2000,
        .start_lat = 39.982, .start_lng = 116.314,
        .end_lat = 39.985, .end_lng = 116.318,
        .duration_sec = 1000, .distance_m = 300,
        .amount_cent = 150, .points = {},
    });
    int bal_before = f.accounts->get_balance(1);

    tutorial::end_ride_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    req.set_end_lat(39.985); req.set_end_lng(116.318);
    auto rsp = parse_end(handlers::end_ride(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.amount_cent(), 150);
    EXPECT_EQ(f.accounts->get_balance(1), bal_before);
}

TEST(EndRide, CrossUserReturns401) {
    Fixture f;
    f.rides->create_with_points({
        .ride_no = "R1", .user_id = 999, .bike_id = 1,
        .start_ts = 1000, .end_ts = 2000,
        .start_lat = 39.982, .start_lng = 116.314,
        .end_lat = 39.985, .end_lng = 116.318,
        .duration_sec = 1000, .distance_m = 300,
        .amount_cent = 150, .points = {},
    });
    tutorial::end_ride_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    req.set_end_lat(39.985); req.set_end_lng(116.318);
    auto rsp = parse_end(handlers::end_ride(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 401);
}

tutorial::report_damage_response parse_damage(const std::vector<std::uint8_t>& b) {
    auto fr = decode(b.data(), b.size());
    tutorial::report_damage_response r;
    if (fr) {
        r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    }
    return r;
}

TEST(ReportDamage, MarksBikeDamaged) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    tutorial::report_damage_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-001");
    req.set_note("刹车失灵");
    auto rsp = parse_damage(handlers::report_damage(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    auto bike = f.bikes->get_for_update("BJ-001");
    EXPECT_EQ(bike->status, BikeStatus::Damaged);
}

TEST(ReportDamage, UnknownBikeReturns404) {
    Fixture f;
    tutorial::report_damage_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-NOSUCH");
    auto rsp = parse_damage(handlers::report_damage(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 404);
}

tutorial::get_ride_detail_response parse_detail(const std::vector<std::uint8_t>& b) {
    auto fr = decode(b.data(), b.size());
    tutorial::get_ride_detail_response r;
    if (fr) {
        r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    }
    return r;
}

TEST(GetRideDetail, ReturnsPointsForOwner) {
    Fixture f;
    std::vector<RidePoint> pts = {
        {.seq = 0, .lat = 39.982, .lng = 116.314, .elapsed_sec = 0},
        {.seq = 1, .lat = 39.983, .lng = 116.315, .elapsed_sec = 5},
        {.seq = 2, .lat = 39.984, .lng = 116.316, .elapsed_sec = 10},
    };
    f.rides->create_with_points({
        .ride_no = "R1", .user_id = 1, .bike_id = 1,
        .start_ts = 1000, .end_ts = 1010,
        .start_lat = 39.982, .start_lng = 116.314,
        .end_lat = 39.984, .end_lng = 116.316,
        .duration_sec = 10, .distance_m = 200,
        .amount_cent = 100, .points = pts,
    });
    tutorial::get_ride_detail_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    auto rsp = parse_detail(handlers::get_ride_detail(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.points_size(), 3);
    EXPECT_EQ(rsp.points(0).lat(), 39.982);
}

TEST(GetRideDetail, CrossUserReturns401) {
    Fixture f;
    f.rides->create_with_points({.ride_no = "RX", .user_id = 999});
    tutorial::get_ride_detail_request req;
    req.set_session_token(f.token); req.set_ride_no("RX");
    auto rsp = parse_detail(handlers::get_ride_detail(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 401);
}

tutorial::list_rides_response parse_list(const std::vector<std::uint8_t>& b) {
    auto fr = decode(b.data(), b.size());
    tutorial::list_rides_response r;
    if (fr) {
        r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    }
    return r;
}

TEST(ListRides, ReturnsUsersRidesOnly) {
    Fixture f;
    f.rides->create_with_points({.ride_no = "R1", .user_id = 1, .amount_cent = 100});
    f.rides->create_with_points({.ride_no = "R2", .user_id = 1, .amount_cent = 150});
    f.rides->create_with_points({.ride_no = "RX", .user_id = 999});
    tutorial::list_rides_request req;
    req.set_session_token(f.token); req.set_limit(20);
    auto rsp = parse_list(handlers::list_rides(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.rides_size(), 2);
}
