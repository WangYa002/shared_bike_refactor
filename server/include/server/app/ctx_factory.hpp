#pragma once

// 模块三: 生产环境 Ctx 装配与 handler 注册(从 bike-server main.cpp 抽取)。
// bike-gateway(inprocess 回退模式)与 bike-dispatch 共享同一装配逻辑,
// 保证两条路径行为一致。RideSessionStore 归业务进程(ring 模式即 Dispatch)。

#include "server/config.hpp"
#include "server/router.hpp"

namespace bike::server {

// 按配置装配全部 repo(mysql 连接池 + redis + 骑行会话内存库)。
Ctx make_prod_ctx(const Config& cfg);

// 注册 FBEB 全协议 12 个事件 handler。
void register_all_handlers(Router& router);

} // namespace bike::server
