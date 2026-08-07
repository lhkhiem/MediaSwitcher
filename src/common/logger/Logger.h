#pragma once

#include <memory>
#include <string>

#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#pragma warning(pop)

class Logger {
public:
    static void init();
    static std::shared_ptr<spdlog::logger> getLogger();

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

// Helper macros for easy logging
#define LOG_INFO(...)  if(Logger::getLogger()) Logger::getLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)  if(Logger::getLogger()) Logger::getLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) if(Logger::getLogger()) Logger::getLogger()->error(__VA_ARGS__)
#define LOG_DEBUG(...) if(Logger::getLogger()) Logger::getLogger()->debug(__VA_ARGS__)
