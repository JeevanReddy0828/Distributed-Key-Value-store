#include "Logger.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>

namespace orcdb {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%dT%H:%M:%S.%e] [%^%l%$] [%n] %v");
    logger_ = std::make_shared<spdlog::logger>("orcdb", consoleSink);
    logger_->set_level(spdlog::level::info);
    spdlog::register_logger(logger_);
}

void Logger::SetNodeId(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(mu_);
    nodeId_ = nodeId;
    logger_->set_pattern(
        fmt::format("[%Y-%m-%dT%H:%M:%S.%e] [%^%l%$] [{}] %v", nodeId));
}

void Logger::SetLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: logger_->set_level(spdlog::level::trace); break;
        case LogLevel::Debug: logger_->set_level(spdlog::level::debug); break;
        case LogLevel::Info:  logger_->set_level(spdlog::level::info);  break;
        case LogLevel::Warn:  logger_->set_level(spdlog::level::warn);  break;
        case LogLevel::Error: logger_->set_level(spdlog::level::err);   break;
        case LogLevel::Fatal: logger_->set_level(spdlog::level::critical); break;
    }
}

void Logger::Trace(const std::string& msg) { logger_->trace(msg); }
void Logger::Debug(const std::string& msg) { logger_->debug(msg); }
void Logger::Info(const std::string& msg)  { logger_->info(msg); }
void Logger::Warn(const std::string& msg)  { logger_->warn(msg); }
void Logger::Error(const std::string& msg) { logger_->error(msg); }
void Logger::Fatal(const std::string& msg) { logger_->critical(msg); }

} // namespace orcdb
