#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <string>

namespace bike::server {

void init_logging(const std::string& level, const std::string& file);

} // namespace bike::server

#define BIKE_LOG_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define BIKE_LOG_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define BIKE_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#define BIKE_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#else
#define BIKE_LOG_DEBUG(...) (void)0
#endif
