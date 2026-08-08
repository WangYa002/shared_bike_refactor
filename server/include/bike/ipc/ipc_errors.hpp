#pragma once

// 模块三 IPC 异常类型 (设计稿 §7.4)。
// 独立于 Linux 粘合层, Gateway worker_batch 只需包含本头即可捕获
// RingSink 抛出的环满/超限异常 → 关闭连接(fail-fast)。跨平台。

#include <stdexcept>

namespace bike::ipc {

// 请求环满: 背压信号, 由 UringEngine::worker_batch 捕获并关闭该连接。
class SinkOverload : public std::runtime_error {
public:
    SinkOverload() : std::runtime_error("ipc request ring full") {}
};

// 请求 payload 超过槽内联上限(kReqPayloadMax): 协议滥用防护, 关连接。
class MalformedIpcRequest : public std::runtime_error {
public:
    MalformedIpcRequest() : std::runtime_error("payload exceeds kReqPayloadMax") {}
};

} // namespace bike::ipc
