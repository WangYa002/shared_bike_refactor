// 模块三 共享内存区域管理实现 (Linux-only, 设计稿 §5.6/§5.7)。

#include "bike/ipc/shm_region.hpp"

#include "server/logging.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <new>
#include <stdexcept>
#include <thread>

namespace bike::ipc {

namespace {

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string shm_name_of(const ShmRegion::Params& p, const char* suffix) {
    return "/" + p.prefix + std::to_string(p.instance) + suffix;
}

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error("ipc shm: " + msg);
}

void* do_mmap(int fd, std::size_t bytes) {
    void* base = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED)
        fail(std::string("mmap failed: ") + std::strerror(errno));
    return base;
}

void write_headers(void* base, std::uint32_t ring_count, std::uint32_t slot_count,
                  std::size_t slot_size, std::size_t total) {
    auto* fh = new (base) ShmFileHeader();
    fh->magic = kShmMagic;
    fh->version = kShmVersion;
    fh->ring_count = ring_count;
    fh->slot_count = slot_count;
    fh->slot_size = static_cast<std::uint32_t>(slot_size);
    fh->total_bytes = total;
    fh->created_ns = now_ns();

    auto* hdrs = reinterpret_cast<RingHeader*>(
        static_cast<std::uint8_t*>(base) + sizeof(ShmFileHeader));
    for (std::uint32_t i = 0; i < ring_count; ++i)
        new (&hdrs[i]) RingHeader();   // NSDMI: head/tail/pid/心跳全部归零
}

// 排他锁(创建方): 防两个 Dispatch 并发启动双写同一 shm。
// LOCK_NB 拿不到即抛(由 compose 重拉重试); 锁随 fd 生命周期,
// 崩溃/退出自动释放, 不显式 unlock。
void lock_file_exclusive(int fd, const std::string& name) {
    if (::flock(fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            fail(name + " is locked by another creator (parallel startup?)");
        fail(name + " flock failed: " + std::strerror(errno));
    }
}

// 对端(Gateway)判活: pid 存活 且 心跳新于阈值 —— 双条件防 pid 复用
// 造成永久启动失败(心跳陈旧/为 0 视为残留)。peer_is_producer:
// req 环对端是 producer(Gateway 写请求环); rsp 环对端是 consumer。
constexpr std::uint64_t kInUseHbLimitNs = 3ull * 1000000000ull;   // 3s

bool peer_in_use(const void* base, bool peer_is_producer) {
    const auto* h0 = reinterpret_cast<const RingHeader*>(
        static_cast<const std::uint8_t*>(base) + sizeof(ShmFileHeader));
    const std::uint32_t pid = peer_is_producer
        ? h0->producer_pid.load(std::memory_order_relaxed)
        : h0->consumer_pid.load(std::memory_order_relaxed);
    if (!ShmRegion::pid_alive(pid)) return false;
    const std::uint64_t hb = peer_is_producer
        ? h0->producer_heartbeat_ns.load(std::memory_order_relaxed)
        : h0->consumer_heartbeat_ns.load(std::memory_order_relaxed);
    if (hb == 0) return false;   // 注册了 pid 但从未心跳 → 残留
    return now_ns() - hb < kInUseHbLimitNs;
}

} // namespace

// ---------------------------------------------------------------- 基础操作

bool ShmRegion::pid_alive(std::uint32_t pid) {
    if (pid == 0) return false;
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno == EPERM;   // 存在但无权限发信号 → 仍视为存活
}

bool ShmRegion::shm_create(const std::string& name, std::size_t total,
                           MappedFile& out) {
    int fd = ::shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        if (errno == EEXIST) return false;
        fail(name + " shm_open(O_CREAT|O_EXCL) failed: " + std::strerror(errno));
    }
    if (::ftruncate(fd, static_cast<off_t>(total)) < 0) {
        const int e = errno;
        ::close(fd);
        ::shm_unlink(name.c_str());
        fail(name + " ftruncate(" + std::to_string(total) +
             ") failed: " + std::strerror(e));
    }
    out.fd = fd;
    out.bytes = total;
    out.shm_name = name;
    out.base = do_mmap(fd, total);
    return true;
}

void ShmRegion::shm_open_existing(const std::string& name, MappedFile& out) {
    int fd = ::shm_open(name.c_str(), O_RDWR, 0);
    if (fd < 0)
        fail(name + " shm_open(O_RDWR) failed: " + std::strerror(errno));
    struct stat st{};
    if (::fstat(fd, &st) < 0) {
        const int e = errno;
        ::close(fd);
        fail(name + " fstat failed: " + std::strerror(e));
    }
    out.fd = fd;
    out.bytes = static_cast<std::size_t>(st.st_size);
    out.shm_name = name;
    out.base = do_mmap(fd, out.bytes);
}

bool ShmRegion::validate_file(const MappedFile& mf, std::uint32_t ring_count,
                              std::uint32_t slot_count, std::size_t slot_size,
                              std::size_t total) {
    if (mf.bytes != total) return false;
    const auto* fh = static_cast<const ShmFileHeader*>(mf.base);
    return fh->magic == kShmMagic &&
           fh->version == kShmVersion &&
           fh->ring_count == ring_count &&
           fh->slot_count == slot_count &&
           fh->slot_size == static_cast<std::uint32_t>(slot_size) &&
           fh->total_bytes == total;
}

void ShmRegion::reinit_file(const MappedFile& mf, std::uint32_t ring_count,
                            std::uint32_t slot_count, std::size_t slot_size,
                            std::size_t total) {
    // 调用前提: mf 已按 total 映射(mmap 尺寸不变, 只需 truncate 归零 + 重写头)
    if (::ftruncate(mf.fd, static_cast<off_t>(total)) < 0)
        fail(mf.shm_name + " reinit ftruncate failed: " + std::strerror(errno));
    write_headers(mf.base, ring_count, slot_count, slot_size, total);
    BIKE_LOG_WARN("ipc shm {} reinitialized (stale region recovered)", mf.shm_name);
}

// ---------------------------------------------------------------- 创建/打开

std::size_t ShmRegion::req_total_bytes() const {
    return compute_file_layout(static_cast<std::uint32_t>(p_.workers),
                               ReqRing::kRegionBytes).total_bytes;
}

std::size_t ShmRegion::rsp_total_bytes() const {
    return compute_file_layout(1, RspRing::kRegionBytes).total_bytes;
}

void ShmRegion::map_all(bool creator_side) {
    const std::uint32_t n = static_cast<std::uint32_t>(p_.workers);

    // ---- req 文件(N 个请求环) ----
    const std::string req_name = shm_name_of(p_, "_req");
    const std::size_t req_total = req_total_bytes();
    bool fresh = false;
    if (creator_side) {
        fresh = shm_create(req_name, req_total, req_);
        if (!fresh) shm_open_existing(req_name, req_);
    } else {
        shm_open_existing(req_name, req_);
    }
    if (creator_side) {
        lock_file_exclusive(req_.fd, req_name);   // 先拿锁再校验/重建, 防并发双写
        if (fresh) {
            write_headers(req_.base, n, ReqRing::kSlotCount, kReqSlotSize, req_total);
        } else if (!validate_file(req_, n, ReqRing::kSlotCount, kReqSlotSize, req_total)) {
            // 残留恢复: 尺寸不对需先 unmap 重映射
            if (req_.bytes != req_total) {
                ::munmap(req_.base, req_.bytes);
                req_.base = do_mmap(req_.fd, req_total);
                req_.bytes = req_total;
            }
            reinit_file(req_, n, ReqRing::kSlotCount, kReqSlotSize, req_total);
        } else {
            // 头合法: 对端(Gateway=req 环 producer)存活即拒绝重建 ——
            // 等其判死退出后由 compose 重拉本进程再接管,
            // 避免新 Dispatch reinit 归零而 Gateway 旧视图仍在映射(恒满/垃圾帧)。
            if (peer_in_use(req_.base, /*peer_is_producer=*/true))
                fail(req_name + " peer (gateway) alive with fresh heartbeat; "
                     "refusing rebuild, will retry after it exits");
            reinit_file(req_, n, ReqRing::kSlotCount, kReqSlotSize, req_total);
        }
    } else if (!validate_file(req_, n, ReqRing::kSlotCount, kReqSlotSize, req_total)) {
        fail(req_name + " header mismatch (size/magic/version/ring_count)");
    }

    // ---- rsp 文件(1 个响应环) ----
    const std::string rsp_name = shm_name_of(p_, "_rsp");
    const std::size_t rsp_total = rsp_total_bytes();
    fresh = false;
    if (creator_side) {
        fresh = shm_create(rsp_name, rsp_total, rsp_);
        if (!fresh) shm_open_existing(rsp_name, rsp_);
    } else {
        shm_open_existing(rsp_name, rsp_);
    }
    if (creator_side) {
        lock_file_exclusive(rsp_.fd, rsp_name);
        if (fresh) {
            write_headers(rsp_.base, 1, RspRing::kSlotCount, kRspSlotSize, rsp_total);
        } else if (!validate_file(rsp_, 1, RspRing::kSlotCount, kRspSlotSize, rsp_total)) {
            if (rsp_.bytes != rsp_total) {
                ::munmap(rsp_.base, rsp_.bytes);
                rsp_.base = do_mmap(rsp_.fd, rsp_total);
                rsp_.bytes = rsp_total;
            }
            reinit_file(rsp_, 1, RspRing::kSlotCount, kRspSlotSize, rsp_total);
        } else {
            // 对端(Gateway=rsp 环 consumer)存活即拒绝重建(同上)
            if (peer_in_use(rsp_.base, /*peer_is_producer=*/false))
                fail(rsp_name + " peer (gateway) alive with fresh heartbeat; "
                     "refusing rebuild, will retry after it exits");
            reinit_file(rsp_, 1, RspRing::kSlotCount, kRspSlotSize, rsp_total);
        }
    } else if (!validate_file(rsp_, 1, RspRing::kSlotCount, kRspSlotSize, rsp_total)) {
        fail(rsp_name + " header mismatch (size/magic/version)");
    }
}

void ShmRegion::unmap_all() {
    if (req_.base != nullptr) { ::munmap(req_.base, req_.bytes); req_.base = nullptr; }
    if (rsp_.base != nullptr) { ::munmap(rsp_.base, rsp_.bytes); rsp_.base = nullptr; }
    if (req_.fd >= 0) { ::close(req_.fd); req_.fd = -1; }
    if (rsp_.fd >= 0) { ::close(rsp_.fd); rsp_.fd = -1; }
}

ShmRegion ShmRegion::create_or_recover(const Params& p) {
    ShmRegion r;
    r.p_ = p;
    r.map_all(true);
    BIKE_LOG_INFO("ipc shm created/recovered: instance={} workers={} req={}MB rsp={}MB",
                  p.instance, p.workers,
                  r.req_total_bytes() / (1024 * 1024),
                  r.rsp_total_bytes() / (1024 * 1024));
    return r;
}

ShmRegion ShmRegion::open(const Params& p) {
    // 重试等待 Dispatch 建环; 超时 fail-fast(由 compose 重拉)
    const auto deadline = std::chrono::steady_clock::now() + p.open_timeout;
    for (;;) {
        try {
            ShmRegion r;
            r.p_ = p;
            r.map_all(false);
            BIKE_LOG_INFO("ipc shm attached: instance={} workers={}", p.instance, p.workers);
            return r;
        } catch (const std::exception& e) {
            if (std::chrono::steady_clock::now() >= deadline)
                fail(std::string("open timeout (") +
                     std::to_string(p.open_timeout.count()) + " ms): " + e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// ---------------------------------------------------------------- attach 视图

std::vector<ReqRing::Consumer> ShmRegion::attach_req_consumers() {
    const RingFileLayout l = compute_file_layout(
        static_cast<std::uint32_t>(p_.workers), ReqRing::kRegionBytes);
    auto* hdrs = reinterpret_cast<RingHeader*>(
        static_cast<std::uint8_t*>(req_.base) + l.headers_offset);
    auto* slots = reinterpret_cast<ReqRing::Slot*>(
        static_cast<std::uint8_t*>(req_.base) + l.slots_offset);
    std::vector<ReqRing::Consumer> out;
    out.reserve(static_cast<std::size_t>(p_.workers));
    for (int i = 0; i < p_.workers; ++i)
        out.emplace_back(&hdrs[i], slots + static_cast<std::size_t>(i) * ReqRing::kSlotCount);
    return out;
}

std::vector<ReqRing::Producer> ShmRegion::attach_req_producers() {
    const RingFileLayout l = compute_file_layout(
        static_cast<std::uint32_t>(p_.workers), ReqRing::kRegionBytes);
    auto* hdrs = reinterpret_cast<RingHeader*>(
        static_cast<std::uint8_t*>(req_.base) + l.headers_offset);
    auto* slots = reinterpret_cast<ReqRing::Slot*>(
        static_cast<std::uint8_t*>(req_.base) + l.slots_offset);
    std::vector<ReqRing::Producer> out;
    out.reserve(static_cast<std::size_t>(p_.workers));
    for (int i = 0; i < p_.workers; ++i)
        out.emplace_back(&hdrs[i], slots + static_cast<std::size_t>(i) * ReqRing::kSlotCount);
    return out;
}

RspRing::Producer ShmRegion::attach_rsp_producer() {
    const RingFileLayout l = compute_file_layout(1, RspRing::kRegionBytes);
    auto* hdr = reinterpret_cast<RingHeader*>(
        static_cast<std::uint8_t*>(rsp_.base) + l.headers_offset);
    auto* slots = reinterpret_cast<RspRing::Slot*>(
        static_cast<std::uint8_t*>(rsp_.base) + l.slots_offset);
    return RspRing::Producer(hdr, slots);
}

RspRing::Consumer ShmRegion::attach_rsp_consumer() {
    const RingFileLayout l = compute_file_layout(1, RspRing::kRegionBytes);
    auto* hdr = reinterpret_cast<RingHeader*>(
        static_cast<std::uint8_t*>(rsp_.base) + l.headers_offset);
    auto* slots = reinterpret_cast<RspRing::Slot*>(
        static_cast<std::uint8_t*>(rsp_.base) + l.slots_offset);
    return RspRing::Consumer(hdr, slots);
}

std::string ShmRegion::req_notify_path() const {
    return p_.shm_root + "/" + p_.prefix + std::to_string(p_.instance) + "_req_notify";
}

std::string ShmRegion::rsp_notify_path() const {
    return p_.shm_root + "/" + p_.prefix + std::to_string(p_.instance) + "_rsp_notify";
}

// ---------------------------------------------------------------- 生命周期

ShmRegion::ShmRegion(ShmRegion&& o) noexcept
    : p_(std::move(o.p_)), req_(std::move(o.req_)), rsp_(std::move(o.rsp_)) {
    o.req_ = MappedFile{};
    o.rsp_ = MappedFile{};
}

ShmRegion& ShmRegion::operator=(ShmRegion&& o) noexcept {
    if (this != &o) {
        unmap_all();
        p_ = std::move(o.p_);
        req_ = std::move(o.req_);
        rsp_ = std::move(o.rsp_);
        o.req_ = MappedFile{};
        o.rsp_ = MappedFile{};
    }
    return *this;
}

ShmRegion::~ShmRegion() {
    // 不 shm_unlink: 残留由下次启动的 create_or_recover 清理,
    // 避免对端尚在映射期 unlink 造成语义混乱(设计稿 §7.6)。
    unmap_all();
}

} // namespace bike::ipc
