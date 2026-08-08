#include "backend_client.hpp"

#include <cstring>
#include <thread>
#include <utility>

namespace bike::client {

BackendClient::BackendClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

std::vector<std::uint8_t> BackendClient::round_trip(std::uint16_t eid,
                                                    const std::string& payload) {
    asio::ip::tcp::socket socket(ioc_);
    asio::ip::tcp::resolver r(ioc_);
    asio::connect(socket, r.resolve(host_, std::to_string(port_)));

    const std::uint32_t seq = next_seq();
    bike::Frame req{.event_id = eid, .seq = seq, .payload = payload};
    auto bytes = bike::encode(req);
    asio::write(socket, asio::buffer(bytes));

    // 新帧头 14 字节: magic(4) + eid u16 LE + seq u32 LE + len i32 LE
    std::uint8_t header[bike::kHeaderLen];
    asio::read(socket, asio::buffer(header, bike::kHeaderLen));
    if (std::memcmp(header, bike::kFrameMagic, 4) != 0)
        throw BackendError("bad magic from server");
    std::uint16_t rsp_eid = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(header[4]) |
        (static_cast<std::uint16_t>(header[5]) << 8));
    std::uint32_t rsp_seq = static_cast<std::uint32_t>(header[6]) |
                            (static_cast<std::uint32_t>(header[7]) << 8) |
                            (static_cast<std::uint32_t>(header[8]) << 16) |
                            (static_cast<std::uint32_t>(header[9]) << 24);
    std::int32_t len = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(header[10]) |
        (static_cast<std::uint32_t>(header[11]) << 8) |
        (static_cast<std::uint32_t>(header[12]) << 16) |
        (static_cast<std::uint32_t>(header[13]) << 24));
    if (len < 0 || static_cast<std::uint32_t>(len) > bike::kMaxMessageLen)
        throw BackendError("bad length from server");
    if (rsp_seq != seq)
        throw BackendError("response seq mismatch");

    std::string body(static_cast<std::size_t>(len), '\0');
    asio::read(socket, asio::buffer(body.data(), body.size()));

    bike::Frame rsp{.event_id = rsp_eid, .seq = rsp_seq, .payload = std::move(body)};
    return bike::encode(rsp);
}

namespace {
template <typename Rsp>
Rsp parse(std::vector<std::uint8_t>& bytes) {
    auto r = bike::decode(bytes.data(), bytes.size());
    if (!r) throw BackendError("failed to decode server response");
    Rsp m;
    if (!m.ParseFromArray(r->frame.payload.data(),
                          static_cast<int>(r->frame.payload.size())))
        throw BackendError("failed to parse protobuf response");
    return m;
}
} // namespace

tutorial::mobile_response BackendClient::get_mobile_code(const std::string& mobile) {
    tutorial::mobile_request req;
    req.set_mobile(mobile);
    auto bytes = round_trip(event_id(Event::MobileRequest), req.SerializeAsString());
    return parse<tutorial::mobile_response>(bytes);
}

tutorial::login_response BackendClient::login(const std::string& mobile, int icode) {
    tutorial::login_request req;
    req.set_mobile(mobile);
    req.set_icode(icode);
    auto bytes = round_trip(event_id(Event::LoginRequest), req.SerializeAsString());
    return parse<tutorial::login_response>(bytes);
}

tutorial::recharge_response BackendClient::recharge(const std::string& token, int amount) {
    tutorial::recharge_request req;
    req.set_session_token(token);
    req.set_amount(amount);
    auto bytes = round_trip(event_id(Event::RechargeRequest), req.SerializeAsString());
    return parse<tutorial::recharge_response>(bytes);
}

tutorial::account_balance_response BackendClient::get_balance(const std::string& token) {
    tutorial::account_balance_request req;
    req.set_session_token(token);
    auto bytes = round_trip(event_id(Event::AccountBalanceRequest), req.SerializeAsString());
    return parse<tutorial::account_balance_response>(bytes);
}

tutorial::list_account_records_response BackendClient::list_records(const std::string& token) {
    tutorial::list_account_records_request req;
    req.set_session_token(token);
    auto bytes = round_trip(event_id(Event::ListAccountRecordsRequest), req.SerializeAsString());
    return parse<tutorial::list_account_records_response>(bytes);
}

tutorial::list_nearby_bikes_response BackendClient::list_nearby_bikes(
    const std::string& token, double lat, double lng, double radius_m) {
    tutorial::list_nearby_bikes_request req;
    req.set_session_token(token);
    req.set_lat(lat);
    req.set_lng(lng);
    req.set_radius_m(radius_m);
    auto bytes = round_trip(event_id(Event::ListNearbyBikesRequest), req.SerializeAsString());
    return parse<tutorial::list_nearby_bikes_response>(bytes);
}

tutorial::scan_unlock_response BackendClient::scan_unlock(
    const std::string& token, const std::string& bike_no,
    double lat, double lng) {
    tutorial::scan_unlock_request req;
    req.set_session_token(token);
    req.set_bike_no(bike_no);
    req.set_lat(lat);
    req.set_lng(lng);
    auto bytes = round_trip(event_id(Event::ScanUnlockRequest), req.SerializeAsString());
    return parse<tutorial::scan_unlock_response>(bytes);
}

tutorial::end_ride_response BackendClient::end_ride(
    const std::string& token, const std::string& ride_no,
    double lat, double lng) {
    tutorial::end_ride_request req;
    req.set_session_token(token);
    req.set_ride_no(ride_no);
    req.set_end_lat(lat);
    req.set_end_lng(lng);
    auto bytes = round_trip(event_id(Event::EndRideRequest), req.SerializeAsString());
    return parse<tutorial::end_ride_response>(bytes);
}

tutorial::report_damage_response BackendClient::report_damage(
    const std::string& token, const std::string& bike_no,
    const std::string& note) {
    tutorial::report_damage_request req;
    req.set_session_token(token);
    req.set_bike_no(bike_no);
    req.set_note(note);
    auto bytes = round_trip(event_id(Event::ReportDamageRequest), req.SerializeAsString());
    return parse<tutorial::report_damage_response>(bytes);
}

tutorial::get_ride_detail_response BackendClient::get_ride_detail(
    const std::string& token, const std::string& ride_no) {
    tutorial::get_ride_detail_request req;
    req.set_session_token(token);
    req.set_ride_no(ride_no);
    auto bytes = round_trip(event_id(Event::GetRideDetailRequest), req.SerializeAsString());
    return parse<tutorial::get_ride_detail_response>(bytes);
}

tutorial::list_rides_response BackendClient::list_rides(
    const std::string& token, int limit) {
    tutorial::list_rides_request req;
    req.set_session_token(token);
    req.set_limit(limit);
    auto bytes = round_trip(event_id(Event::ListRidesRequest), req.SerializeAsString());
    return parse<tutorial::list_rides_response>(bytes);
}

void BackendClient::report_position(
    const std::string& ride_no, int seq,
    double lat, double lng, int elapsed_sec) {
    // Backpressure:如果上一次还未发完,直接丢弃本次。
    bool expected = false;
    if (!pos_sending_.compare_exchange_strong(expected, true)) return;

    tutorial::ride_position_report req;
    req.set_ride_no(ride_no);
    req.set_seq(seq);
    req.set_lat(lat);
    req.set_lng(lng);
    req.set_elapsed_sec(elapsed_sec);

    // 在分离线程里走独立 socket 发送,不阻塞调用方
    std::thread([this, req] {
        try {
            asio::io_context ioc;
            asio::ip::tcp::socket socket(ioc);
            asio::ip::tcp::resolver r(ioc);
            asio::connect(socket, r.resolve(host_, std::to_string(port_)));
            bike::Frame frame{.event_id = bike::event_id(bike::Event::RidePositionReport),
                              .seq = next_seq(),
                              .payload = req.SerializeAsString()};
            auto bytes = bike::encode(frame);
            asio::write(socket, asio::buffer(bytes));
            // 不读响应(单向事件)
        } catch (...) {
            // 静默失败,下秒覆盖
        }
        pos_sending_.store(false);
    }).detach();
}

} // namespace bike::client
