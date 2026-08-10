#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "server/logging.hpp"
#include "bike/geo.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>

#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace bike::server::handlers {

namespace {

// ---- 动态投放参数 ----
// 防抖策略: 不引入区域缓存/定时器, 仅当半径内车辆 < kSpawnThreshold 时
// 才投放并补到 kSpawnTarget 辆 —— 投放后该区域立即达标, 后续请求
// 自然不再触发, 收敛于每区域至多一波投放(车辆被租走/报修降至
// 阈值以下后才会再次投放)。
constexpr int kSpawnThreshold = 5;
constexpr int kSpawnTarget = 12;
constexpr double kSpawnRadiusFactor = 0.8;  // 在 0.8 倍半径内散布, 避免贴边查不到
constexpr int kMaxNoRetry = 4;              // 车号冲突重试上限

std::mt19937& spawn_rng() {
    thread_local std::mt19937 rng{std::random_device{}()};
    return rng;
}

// 车号风格对齐种子数据(前缀-编号), 用 DY- 前缀区分动态投放车;
// 6 位去混淆字符集随机后缀, 碰撞概率极低, 冲突时由调用方重试。
std::string make_dyn_bike_no() {
    static const char kAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    std::uniform_int_distribution<int> pick(0, sizeof(kAlphabet) - 2);
    std::string no = "DY-";
    for (int i = 0; i < 6; ++i) no += kAlphabet[pick(spawn_rng())];
    return no;
}

// 在请求点周围 0.8 倍半径圆盘内均匀采样一个投放点。
// sqrt(r) 变换保证面积均匀; 经纬度偏移按 lat/lng 各自米制换算。
void random_offset(double center_lat, double center_lng, double radius_m,
                   double& lat, double& lng) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    auto& rng = spawn_rng();
    const double rr = radius_m * kSpawnRadiusFactor * std::sqrt(u(rng));
    const double theta = 2.0 * 3.14159265358979 * u(rng);
    const double d_east = rr * std::cos(theta);   // 东西向米
    const double d_north = rr * std::sin(theta);  // 南北向米
    lat = center_lat + d_north / 111000.0;
    const double cos_lat = std::cos(center_lat * 3.14159265358979 / 180.0);
    lng = center_lng + d_east / (111000.0 * (cos_lat < 0.01 ? 0.01 : cos_lat));
}

// 投放 need 辆 idle 车并持久化, 返回成功入库的车辆(带 id)。
std::vector<Bike> spawn_bikes(Ctx& ctx, double lat, double lng, double r, int need) {
    std::vector<Bike> spawned;
    for (int i = 0; i < need; ++i) {
        double plat = 0, plng = 0;
        random_offset(lat, lng, r, plat, plng);
        std::optional<Bike> ok;
        for (int attempt = 0; attempt < kMaxNoRetry; ++attempt) {
            ok = ctx.bikes->insert(Bike{.id = 0, .bike_no = make_dyn_bike_no(),
                                        .lat = plat, .lng = plng,
                                        .status = BikeStatus::Idle});
            if (ok) break;  // 冲突(UNIQUE)则换号重试
        }
        if (ok) {
            spawned.push_back(*ok);
        } else {
            BIKE_LOG_WARN("spawn_bikes: insert failed after retries");
        }
    }
    if (!spawned.empty())
        BIKE_LOG_INFO("spawn {} dynamic bikes near ({:.5f},{:.5f}) r={:.0f}m",
                      spawned.size(), lat, lng, r);
    return spawned;
}

} // namespace

std::vector<std::uint8_t> list_nearby_bikes(const std::string& payload, Ctx& ctx) {
    tutorial::list_nearby_bikes_response rsp;
    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = event_id(Event::ListNearbyBikesResponse), .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::list_nearby_bikes_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    if (!valid_lat(req.lat()) || !valid_lng(req.lng()))
        return fail(ErrCode::InvalidData);

    double r = clamp_radius(req.radius_m());
    double dlat = r / 111000.0;
    double dlng = r / (111000.0 * std::cos(req.lat() * 3.14159265358979 / 180.0));

    auto bikes = ctx.bikes->list_in_bounds(
        req.lat() - dlat, req.lat() + dlat,
        req.lng() - dlng, req.lng() + dlng);

    // 圆形半径过滤(box 查询的角落可能在半径外)。
    std::vector<Bike> near;
    near.reserve(bikes.size());
    for (const auto& b : bikes) {
        if (haversine_m(req.lat(), req.lng(), b.lat, b.lng) <= r) near.push_back(b);
    }

    // 稀疏区域动态投放(见上方防抖策略注释)。
    if (static_cast<int>(near.size()) < kSpawnThreshold) {
        auto spawned = spawn_bikes(ctx, req.lat(), req.lng(), r,
                                   kSpawnTarget - static_cast<int>(near.size()));
        for (auto& b : spawned) near.push_back(std::move(b));
    }

    rsp.set_code(code(ErrCode::Ok));
    for (const auto& b : near) {
        auto* bi = rsp.add_bikes();
        bi->set_bike_no(b.bike_no);
        bi->set_lat(b.lat);
        bi->set_lng(b.lng);
        bi->set_status(static_cast<int>(b.status));
    }
    Frame f{.event_id = event_id(Event::ListNearbyBikesResponse), .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
