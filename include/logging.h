#ifndef LOGGING_H
#define LOGGING_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>
#include <memory>

// Logging configuration
#define LOG_FILE_SIZE (200 * 1024 * 1024)  // 200MB per file
#define LOG_FILE_COUNT 40                   // Keep 40 rotating files
#define ASYNC_QUEUE_SIZE 32768              // Large async queue for high throughput

extern std::shared_ptr<spdlog::logger> g_logger;
extern std::shared_ptr<spdlog::logger> g_console_logger;

void init_logging();

#endif
