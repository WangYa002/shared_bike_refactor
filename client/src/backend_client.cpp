#include "backend_client.hpp"

#include <cstring>

namespace bike::client {

BackendClient::BackendClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

std::vector<std::uint8_t> BackendClient::round_trip(std::uint16_t eid,
                                                    const std::string& payload) {
    asio::ip::tcp::socket socket(ioc_);
    asio::ip::tcp::resolver r(ioc_);
    asio::connect(socket, r.resolve(host_, std::to_string(port_)));

    bike::Frame req{.event_id = eid, .payload = payload};
    auto bytes = bike::encode(req);
    asio::write(socket, asio::buffer(bytes));

    std::uint8_t header[bike::kHeaderLen];
    asio::read(socket, asio::buffer(header, bike::kHeaderLen));
    if (std::memcmp(header, bike::kFrameMagic, 4) != 0)
        throw BackendError("bad magic from server");
    std::int32_t len = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(header[6]) |
        (static_cast<std::uint32_t>(header[7]) << 8) |
        (static_cast<std::uint32_t>(header[8]) << 16) |
        (static_cast<std::uint32_t>(header[9]) << 24));
    if (len < 0 || static_cast<std::uint32_t>(len) > bike::kMaxMessageLen)
        throw BackendError("bad length from server");

    std::string body(static_cast<std::size_t>(len), '\0');
    asio::read(socket, asio::buffer(body.data(), body.size()));

    bike::Frame rsp{.event_id = eid, .payload = body};
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
    auto bytes = round_trip(0x01, req.SerializeAsString());
    return parse<tutorial::mobile_response>(bytes);
}

tutorial::login_response BackendClient::login(const std::string& mobile, int icode) {
    tutorial::login_request req;
    req.set_mobile(mobile);
    req.set_icode(icode);
    auto bytes = round_trip(0x03, req.SerializeAsString());
    return parse<tutorial::login_response>(bytes);
}

tutorial::recharge_response BackendClient::recharge(const std::string& token, int amount) {
    tutorial::recharge_request req;
    req.set_session_token(token);
    req.set_amount(amount);
    auto bytes = round_trip(0x05, req.SerializeAsString());
    return parse<tutorial::recharge_response>(bytes);
}

tutorial::account_balance_response BackendClient::get_balance(const std::string& token) {
    tutorial::account_balance_request req;
    req.set_session_token(token);
    auto bytes = round_trip(0x07, req.SerializeAsString());
    return parse<tutorial::account_balance_response>(bytes);
}

tutorial::list_account_records_response BackendClient::list_records(const std::string& token) {
    tutorial::list_account_records_request req;
    req.set_session_token(token);
    auto bytes = round_trip(0x09, req.SerializeAsString());
    return parse<tutorial::list_account_records_response>(bytes);
}

} // namespace bike::client
