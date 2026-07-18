#pragma once

#include "server/repo/session_store.hpp"

#include <hiredis/hiredis.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bike::server {

class RedisSessionStore : public ISessionStore {
public:
    RedisSessionStore(std::string host, int port, int pool_size);
    ~RedisSessionStore() override;

    int set_code(const std::string& mobile) override;
    std::optional<int> get_code(const std::string& mobile) override;
    void delete_code(const std::string& mobile) override;
    std::string create_session(const std::string& mobile) override;
    std::optional<std::string> lookup_session(const std::string& token) override;
    void destroy_session(const std::string& token) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bike::server
