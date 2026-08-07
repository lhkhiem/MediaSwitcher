#include "Logger.h"

#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#pragma warning(pop)
#include <vector>
#include <iostream>
#include <filesystem>

std::shared_ptr<spdlog::logger> Logger::s_logger;

void Logger::init() {
    try {
        std::filesystem::create_directories("logs");

        // Console sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

        // File sink
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/mediaswitcher.log", true);
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};

        s_logger = std::make_shared<spdlog::logger>("MediaSwitcher", sinks.begin(), sinks.end());
        s_logger->set_level(spdlog::level::debug);
        s_logger->flush_on(spdlog::level::trace);
        spdlog::flush_every(std::chrono::seconds(1));

        spdlog::register_logger(s_logger);
        spdlog::set_default_logger(s_logger);

        LOG_INFO("Logger initialized successfully.");
    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
    }
}

std::shared_ptr<spdlog::logger> Logger::getLogger() {
    return s_logger;
}
