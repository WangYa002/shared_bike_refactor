#include "server/handlers.hpp"
#include "server/repo/in_memory.hpp"

#include <bike/protocol.hpp>
#include <bike.pb.h>

#include <gtest/gtest.h>

using namespace bike;
using namespace bike::server;

namespace {

struct Env {
    std::shared_ptr<InMemoryUserRepo> users{std::make_shared<InMemoryUserRepo>()};
    std::shared_ptr<InMemoryAccountRepo> accs{std::make_shared<InMemoryAccountRepo>()};
    std::shared_ptr<InMemorySessionStore> sess{std::make_shared<InMemorySessionStore>()};
    Ctx ctx{users, accs, sess};
};

Frame parse_frame(const std::vector<std::uint8_t>& bytes) {
    auto r = decode(bytes.data(), bytes.size());
    EXPECT_TRUE(r.has_value());
    return r->frame;
}

tutorial::mobile_response as_mobile_rsp(const std::vector<std::uint8_t>& bytes) {
    auto f = parse_frame(bytes);
    tutorial::mobile_response m;
    EXPECT_TRUE(m.ParseFromArray(f.payload.data(), static_cast<int>(f.payload.size())));
    return m;
}

tutorial::login_response as_login_rsp(const std::vector<std::uint8_t>& bytes) {
    auto f = parse_frame(bytes);
    tutorial::login_response m;
    EXPECT_TRUE(m.ParseFromArray(f.payload.data(), static_cast<int>(f.payload.size())));
    return m;
}

tutorial::recharge_response as_recharge_rsp(const std::vector<std::uint8_t>& bytes) {
    auto f = parse_frame(bytes);
    tutorial::recharge_response m;
    EXPECT_TRUE(m.ParseFromArray(f.payload.data(), static_cast<int>(f.payload.size())));
    return m;
}

tutorial::account_balance_response as_balance_rsp(const std::vector<std::uint8_t>& bytes) {
    auto f = parse_frame(bytes);
    tutorial::account_balance_response m;
    EXPECT_TRUE(m.ParseFromArray(f.payload.data(), static_cast<int>(f.payload.size())));
    return m;
}

tutorial::list_account_records_response as_records_rsp(const std::vector<std::uint8_t>& bytes) {
    auto f = parse_frame(bytes);
    tutorial::list_account_records_response m;
    EXPECT_TRUE(m.ParseFromArray(f.payload.data(), static_cast<int>(f.payload.size())));
    return m;
}

} // namespace

TEST(Handlers, MobileCodeReturns6Digit) {
    Env env;
    tutorial::mobile_request req;
    req.set_mobile("15600000000");
    auto bytes = handlers::mobile_code(req.SerializeAsString(), env.ctx);
    auto m = as_mobile_rsp(bytes);
    EXPECT_EQ(m.code(), 200);
    EXPECT_GE(m.icode(), 100000);
    EXPECT_LE(m.icode(), 999999);
}

TEST(Handlers, MobileCodeEventId) {
    Env env;
    tutorial::mobile_request req;
    req.set_mobile("15600000000");
    auto bytes = handlers::mobile_code(req.SerializeAsString(), env.ctx);
    auto f = parse_frame(bytes);
    EXPECT_EQ(f.event_id, 0x02);
}

TEST(Handlers, LoginWrongCodeFails) {
    Env env;
    tutorial::mobile_request mreq;
    mreq.set_mobile("15600000000");
    handlers::mobile_code(mreq.SerializeAsString(), env.ctx);

    tutorial::login_request lr;
    lr.set_mobile("15600000000");
    lr.set_icode(999999);
    auto bytes = handlers::login(lr.SerializeAsString(), env.ctx);
    auto lresp = as_login_rsp(bytes);
    EXPECT_EQ(lresp.code(), 404);
    EXPECT_TRUE(lresp.session_token().empty());
}

TEST(Handlers, LoginCorrectCodeSucceedsAndReturnsToken) {
    Env env;
    tutorial::mobile_request mreq;
    mreq.set_mobile("15600000001");
    auto mc_bytes = handlers::mobile_code(mreq.SerializeAsString(), env.ctx);
    int code = as_mobile_rsp(mc_bytes).icode();

    tutorial::login_request lr;
    lr.set_mobile("15600000001");
    lr.set_icode(code);
    auto bytes = handlers::login(lr.SerializeAsString(), env.ctx);
    auto lresp = as_login_rsp(bytes);
    EXPECT_EQ(lresp.code(), 200);
    EXPECT_FALSE(lresp.session_token().empty());
}

TEST(Handlers, RechargeRequiresSession) {
    Env env;
    tutorial::recharge_request rr;
    rr.set_session_token("invalid");
    rr.set_amount(1000);
    auto bytes = handlers::recharge(rr.SerializeAsString(), env.ctx);
    auto rsp = as_recharge_rsp(bytes);
    EXPECT_EQ(rsp.code(), 401);
}

TEST(Handlers, RechargeZeroAmountFails) {
    Env env;
    tutorial::mobile_request mreq;
    mreq.set_mobile("15600000002");
    int code = as_mobile_rsp(handlers::mobile_code(mreq.SerializeAsString(), env.ctx)).icode();
    tutorial::login_request lr;
    lr.set_mobile("15600000002");
    lr.set_icode(code);
    auto token = as_login_rsp(handlers::login(lr.SerializeAsString(), env.ctx)).session_token();

    tutorial::recharge_request rr;
    rr.set_session_token(token);
    rr.set_amount(0);
    auto bytes = handlers::recharge(rr.SerializeAsString(), env.ctx);
    auto rsp = as_recharge_rsp(bytes);
    EXPECT_EQ(rsp.code(), 404);
}

TEST(Handlers, FullFlowLoginRechargeBalanceRecords) {
    Env env;
    tutorial::mobile_request mreq;
    mreq.set_mobile("15600000003");
    int code = as_mobile_rsp(handlers::mobile_code(mreq.SerializeAsString(), env.ctx)).icode();
    tutorial::login_request lr;
    lr.set_mobile("15600000003");
    lr.set_icode(code);
    auto token = as_login_rsp(handlers::login(lr.SerializeAsString(), env.ctx)).session_token();
    ASSERT_FALSE(token.empty());

    tutorial::recharge_request rr;
    rr.set_session_token(token);
    rr.set_amount(1000);
    auto rb = handlers::recharge(rr.SerializeAsString(), env.ctx);
    EXPECT_EQ(as_recharge_rsp(rb).balance(), 1000);

    rr.set_amount(500);
    auto rb2 = handlers::recharge(rr.SerializeAsString(), env.ctx);
    EXPECT_EQ(as_recharge_rsp(rb2).balance(), 1500);

    tutorial::account_balance_request br;
    br.set_session_token(token);
    auto bb = handlers::account_balance(br.SerializeAsString(), env.ctx);
    EXPECT_EQ(as_balance_rsp(bb).balance(), 1500);

    tutorial::list_account_records_request rrq;
    rrq.set_session_token(token);
    auto recs_bytes = handlers::list_records(rrq.SerializeAsString(), env.ctx);
    auto recs = as_records_rsp(recs_bytes);
    EXPECT_EQ(recs.records_size(), 2);
    EXPECT_EQ(recs.records(0).amount(), 500);
    EXPECT_EQ(recs.records(1).amount(), 1000);
}
