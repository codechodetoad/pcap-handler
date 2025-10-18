#include "logging.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>
#include <sys/stat.h>
#include <iostream>
#include <cstring>
#include <cerrno>

std::shared_ptr<spdlog::logger> g_logger;
std::shared_ptr<spdlog::logger> g_console_logger;

/**
 * Initialize spdlog with optimized settings for high-volume logging
 */
void init_logging() {
    try {
        // Create logs directory if it doesn't exist
        struct stat st = {0};
        if (stat("logs", &st) == -1) {
            if (mkdir("logs", 0755) == -1) {
                std::cerr << "WARNING: Failed to create logs directory: " << strerror(errno) << std::endl;
                std::cerr << "Will attempt to continue anyway..." << std::endl;
            }
        }

        // Initialize async logging with large queue
        spdlog::init_thread_pool(ASYNC_QUEUE_SIZE, 2); // 2 background threads

        // Create rotating file sink for main packet logs
        auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/packets.log", LOG_FILE_SIZE, LOG_FILE_COUNT);

        // Create console sink for important messages
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        // Create async file logger with optimized pattern
        g_logger = std::make_shared<spdlog::async_logger>(
            "packet_logger",
            rotating_sink,
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);

        // Create console logger
        g_console_logger = std::make_shared<spdlog::logger>("console", console_sink);

        // Set logging patterns for maximum performance
        g_logger->set_pattern("%v");  // Only message, no timestamp for speed
        g_console_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v");

        // Set levels
        g_logger->set_level(spdlog::level::info);
        g_console_logger->set_level(spdlog::level::info);

        // Force flush every N messages for data safety
        g_logger->flush_on(spdlog::level::warn);

        // Register loggers
        spdlog::register_logger(g_logger);
        spdlog::register_logger(g_console_logger);

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "FATAL: spdlog initialization failed: " << ex.what() << std::endl;
        exit(1);
    }
}