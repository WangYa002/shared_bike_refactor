#pragma once

#include <bike/protocol.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/repo/user_repo.hpp"
#include "server/repo/account_repo.hpp"
#include "server/repo/session_store.hpp"
#include "server/repo/bike_repo.hpp"
#include "server/repo/ride_repo.hpp"
#include "server/ride_session_store.hpp"

namespace bike::server {

struct Ctx {
    std::shared_ptr<IUserRepo>         users;
    std::shared_ptr<IAccountRepo>      accounts;
    std::shared_ptr<ISessionStore>     sessions;
    std::shared_ptr<IBikeRepo>         bikes;
    std::shared_ptr<IRideRepo>         rides;
    std::shared_ptr<RideSessionStore>  ride_sessions;
};

using HandlerFn = std::function<std::vector<std::uint8_t>(
    const std::string& payload, Ctx& ctx)>;

class Router {
public:
    void register_handler(std::uint16_t eid, HandlerFn fn);
    std::vector<std::uint8_t> dispatch(std::uint16_t eid,
                                       const std::string& payload,
                                       Ctx& ctx);
private:
    std::unordered_map<std::uint16_t, HandlerFn> handlers_;
};

} // namespace bike::server
