#include "server/handlers.hpp"
#include "server/repo/in_memory.hpp"
#include "server/ride_session_store.hpp"
#include "server/router.hpp"
#include "bike/protocol.hpp"

#include <bike.pb.h>
#include <gtest/gtest.h>

#include <ctime>

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
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    f.bikes->seed({.id = 2, .bike_no = "BJ-002", .lat = 39.983, .lng = 116.315, .status = BikeStatus::Damaged});
    f.bikes->seed({.id = 3, .bike_no = "BJ-003", .lat = 40.000, .lng = 116.500, .status = BikeStatus::Idle});

    tutorial::list_nearby_bikes_request req;
    req.set_session_token(f.token);
    req.set_lat(39.982);
    req.set_lng(116.314);
    req.set_radius_m(500);

    auto bytes = handlers::list_nearby_bikes(req.SerializeAsString(), f.ctx);
    auto rsp = parse_nearby(bytes);
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.bikes_size(), 2);
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

