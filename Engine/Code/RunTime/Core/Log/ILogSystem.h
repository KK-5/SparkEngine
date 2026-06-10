#pragma once

#include <type_traits>
#include <vector>
#include <string>

#include <spdlog/fmt/bundled/format.h>

#include <Service/Service.h>

namespace Spark
{
    enum class LogLevel : uint8_t
    {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical
    };

    struct LogConfig
    {
        LogConfig(bool showTimeStamp, bool showThreadId, bool useColor, bool async, LogLevel level):
            m_showTimeStamp(showTimeStamp),
            m_showThreadId(showThreadId),
            m_useColor(useColor),
            m_async(async),
            m_level(level)
        {}

        LogConfig() = default;

        bool m_showTimeStamp = false;
        bool m_showThreadId = false;
        bool m_useColor = true;
        bool m_async = true;
        LogLevel m_level = LogLevel::Trace;
    };

    /// Abstract log system interface.
    ///
    /// Concrete backends implement LogV (type-erased format_args) and the
    /// virtual management methods (GetLogs, Reset). The variadic Log()
    /// template erases arguments at the call site and dispatches through
    /// the virtual LogV — no CRTP needed, consumers only see ILogSystem.
    class ILogSystem
    {
    public:
        ILogSystem() = default;
        virtual ~ILogSystem() = default;

        // -- Variadic entry points (inline, non-virtual) -------------------

        /// Format string + arguments (e.g. Log(level, "hello {}", 42)).
        template<typename... Args>
        void Log(LogLevel level, fmt::string_view fmt, const Args&... args)
        {
            LogV(level, fmt, fmt::make_format_args(args...));
        }

        /// Single value, no format string (e.g. Log(level, 42)).
        /// Disabled when T is convertible to a format string to avoid
        /// ambiguity with the variadic overload above.
        template<typename T,
                 typename = std::enable_if_t<!std::is_convertible_v<T, fmt::string_view>>>
        void Log(LogLevel level, const T& value)
        {
            LogV(level, "{}", fmt::make_format_args(value));
        }

        // -- Type-erased virtual (one per backend) -------------------------
        virtual void LogV(LogLevel level, fmt::string_view fmt, fmt::format_args args) = 0;

        // -- Management ----------------------------------------------------
        virtual std::vector<std::string> GetLogs() = 0;
        virtual void Reset(LogConfig config)       = 0;
    };
}

// -- Logging macros ----------------------------------------------------------

#ifdef NODEBUG
#define LOG_HELPER(LOG_LEVEL, ...) \
    Service<ILogSystem>::Get()->Log(LOG_LEVEL, __VA_ARGS__);
#else
#define LOG_HELPER(LOG_LEVEL, ...) \
    (Service<ILogSystem>::Get() ? Service<ILogSystem>::Get()->Log(LOG_LEVEL, __VA_ARGS__) : void(0));
#endif

#ifdef NODEBUG
#define LOG_RESET(...) Service<ILogSystem>::Get()->Reset(__VA_ARGS__);
#else
#define LOG_RESET(...) (Service<ILogSystem>::Get() ? Service<ILogSystem>::Get()->Reset(__VA_ARGS__) : void(0));
#endif

#define LOG_DEBUG(...) LOG_HELPER(LogLevel::Debug, __VA_ARGS__)

#define LOG_INFO(...) LOG_HELPER(LogLevel::Info, __VA_ARGS__)

#define LOG_WARN(...) LOG_HELPER(LogLevel::Warn, __VA_ARGS__)

#define LOG_ERROR(...) LOG_HELPER(LogLevel::Error, __VA_ARGS__)

// Cross-platform debugger break. No-op in shipping builds.
#if defined(NDEBUG)
    #define SPARK_DEBUGBREAK() ((void)0)
#elif defined(_MSC_VER)
    #define SPARK_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define SPARK_DEBUGBREAK() __builtin_debugtrap()
#else
    #include <csignal>
    #define SPARK_DEBUGBREAK() std::raise(SIGTRAP)
#endif

#define LOG_CIRTICAL(...) \
    do { \
        LOG_HELPER(LogLevel::Critical, __VA_ARGS__); \
        SPARK_DEBUGBREAK(); \
    } while (0)

#ifdef NODEBUG
#define ASSERT(expression, ...)
#else
#define ASSERT(expression, ...)                                                                   \
    do                                                                                            \
    {                                                                                             \
        (void)sizeof(expression);                                                                 \
        if (!(expression))                                                                        \
        {                                                                                         \
            LOG_CIRTICAL("{}:{} {}", __FILE__, __LINE__, fmt::format(__VA_ARGS__));               \
        }                                                                                         \
    } while (0)
#endif
