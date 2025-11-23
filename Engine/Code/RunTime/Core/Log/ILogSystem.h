#pragma once

#include <vector>
#include <string>

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

    template <typename D>
    class ILogSystem
    {
    public:
        ILogSystem() = default;
        virtual ~ILogSystem() = default;
        
        template<typename... Args>
        void Log(LogLevel level, Args&&... args)
        {
            return static_cast<D*>(this)->LogImpl(level, std::forward<Args>(args)...);
        }

        virtual std::vector<std::string> GetLogs() = 0;
        virtual void Reset(LogConfig config)       = 0;
    };
}


