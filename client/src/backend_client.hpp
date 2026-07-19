#pragma once

#include <bike/protocol.hpp>
#include <bike.pb.h>

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bike::client {

class BackendError : public std::runtime_error {
public:
    BackendError(const std::string& msg) : std::runtime_error(msg) {}
};

class BackendClient {
public:
    BackendClient(std::string host, int port);

    // 现有方法
    tutorial::mobile_response              get_mobile_code(const std::string& mobile);
    tutorial::login_response               login(const std::string& mobile, int icode);
    tutorial::recharge_response            recharge(const std::string& token, int amount);
    tutorial::account_balance_response     get_balance(const std::string& token);
    tutorial::list_account_records_response list_records(const std::string& token);

    // 新增 7 个方法
    tutorial::list_nearby_bikes_response list_nearby_bikes(
        const std::string& token, double lat, double lng, double radius_m);
    tutorial::scan_unlock_response scan_unlock(
        const std::string& token, const std::string& bike_no,
        double lat, double lng);
    void report_position(
        const std::string& ride_no, int seq,
        double lat, double lng, int elapsed_sec);  // 单向,fire-and-forget
    tutorial::end_ride_response end_ride(
        const std::string& token, const std::string& ride_no,
        double lat, double lng);
    tutorial::report_damage_response report_damage(
        const std::string& token, const std::string& bike_no,
        const std::string& note);
    tutorial::get_ride_detail_response get_ride_detail(
        const std::string& token, const std::string& ride_no);
    tutorial::list_rides_response list_rides(
        const std::string& token, int limit);

private:
    std::vector<std::uint8_t> round_trip(std::uint16_t eid, const std::string& payload);

    std::string host_;
    int port_;
    asio::io_context ioc_;
    std::atomic<bool> pos_sending_{false};  // 上报位置时的 backpressure flag
};

} // namespace bike::client
