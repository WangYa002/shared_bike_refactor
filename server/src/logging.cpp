#include "server/logging.hpp"

#include <stdexcept>

namespace bike::server {

void init_logging(const std::string& level, const std::string& file) {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto rotating = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file, /*max_size=*/5 * 1024 * 1024, /*max_files=*/3);

    auto logger = std::make_shared<spdlog::logger>(
        "bike", spdlog::sinks_init_list{console, rotating});

    if (level == "debug")           logger->set_level(spdlog::level::debug);
    else if (level == "info")       logger->set_level(spdlog::level::info);
    else if (level == "warn")       logger->set_level(spdlog::level::warn);
    else if (level == "error")      logger->set_level(spdlog::level::err);
    else                            logger->set_level(spdlog::level::info);

    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
}

} // namespace bike::server
