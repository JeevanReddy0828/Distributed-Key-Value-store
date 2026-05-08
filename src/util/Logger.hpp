#pragma once
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Use spdlog's own fmt wrapper — works with both bundled and external fmt
#ifdef SPDLOG_FMT_EXTERNAL
#  include <fmt/format.h>
#else
#  include <spdlog/fmt/bundled/format.h>
#endif

namespace orcdb {

namespace detail { using namespace fmt; }

enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal };

class Logger {
public:
    static Logger& Instance();

    void SetLevel(LogLevel level);
    void SetNodeId(const std::string& nodeId);

    void Trace(const std::string& msg);
    void Debug(const std::string& msg);
    void Info(const std::string& msg);
    void Warn(const std::string& msg);
    void Error(const std::string& msg);
    void Fatal(const std::string& msg);

    template<typename... Args>
    void InfoF(detail::format_string<Args...> f, Args&&... args) {
        Info(detail::format(f, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void WarnF(detail::format_string<Args...> f, Args&&... args) {
        Warn(detail::format(f, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void ErrorF(detail::format_string<Args...> f, Args&&... args) {
        Error(detail::format(f, std::forward<Args>(args)...));
    }

    template<typename... Args>
    void DebugF(detail::format_string<Args...> f, Args&&... args) {
        Debug(detail::format(f, std::forward<Args>(args)...));
    }

private:
    Logger();
    std::shared_ptr<spdlog::logger> logger_;
    std::string nodeId_;
    std::mutex mu_;
};

} // namespace orcdb

#define LOG_INFO(msg)  orcdb::Logger::Instance().Info(msg)
#define LOG_WARN(msg)  orcdb::Logger::Instance().Warn(msg)
#define LOG_ERROR(msg) orcdb::Logger::Instance().Error(msg)
#define LOG_DEBUG(msg) orcdb::Logger::Instance().Debug(msg)
#define LOG_FATAL(msg) orcdb::Logger::Instance().Fatal(msg)

#define LOG_INFOF(fmt, ...)  orcdb::Logger::Instance().InfoF(fmt, ##__VA_ARGS__)
#define LOG_WARNF(fmt, ...)  orcdb::Logger::Instance().WarnF(fmt, ##__VA_ARGS__)
#define LOG_ERRORF(fmt, ...) orcdb::Logger::Instance().ErrorF(fmt, ##__VA_ARGS__)
#define LOG_DEBUGF(fmt, ...) orcdb::Logger::Instance().DebugF(fmt, ##__VA_ARGS__)
