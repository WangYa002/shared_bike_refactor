#pragma once

#include <bike/protocol.hpp>
#include <bike.pb.h>

#include <asio.hpp>

#include <cstdint>
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

    tutorial::mobile_response              get_mobile_code(const std::string& mobile);
    tutorial::login_response               login(const std::string& mobile, int icode);
    tutorial::recharge_response            recharge(const std::string& token, int amount);
    tutorial::account_balance_response     get_balance(const std::string& token);
    tutorial::list_account_records_response list_records(const std::string& token);

private:
    std::vector<std::uint8_t> round_trip(std::uint16_t eid, const std::string& payload);

    std::string host_;
    int port_;
    asio::io_context ioc_;
};

} // namespace bike::client
