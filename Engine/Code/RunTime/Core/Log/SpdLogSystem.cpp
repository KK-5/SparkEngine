
#include "SpdLogSystem.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace Spark
{
    SpdLogSystem::SpdLogSystem(LogConfig logConfig)
    {
        InitSpdLogger(logConfig);
    }

    void SpdLogSystem::InitSpdLogger(LogConfig logConfig)
    {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        m_ringBufferSink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(500);

        switch (logConfig.m_level)
        {
        case LogLevel::Trace:
            console_sink->set_level(spdlog::level::trace);
            m_ringBufferSink->set_level(spdlog::level::trace);
            break;
        case LogLevel::Debug:
            console_sink->set_level(spdlog::level::debug);
            m_ringBufferSink->set_level(spdlog::level::debug);
            break;
        case LogLevel::Info:
            console_sink->set_level(spdlog::level::info);
            m_ringBufferSink->set_level(spdlog::level::info);
            break;
        case LogLevel::Warn:
            console_sink->set_level(spdlog::level::warn);
            m_ringBufferSink->set_level(spdlog::level::warn);
            break;
        case LogLevel::Error:
            console_sink->set_level(spdlog::level::err);
            m_ringBufferSink->set_level(spdlog::level::err);
            break;
        case LogLevel::Critical:
            console_sink->set_level(spdlog::level::critical);
            m_ringBufferSink->set_level(spdlog::level::critical);
            break;
        default:
            break;
        }
        std::string pattern = "[%l] %v";
        if (logConfig.m_useColor)
        {
            pattern.replace(pattern.begin(), pattern.begin() + 4, "[%^%l%$]");
        }

        if (logConfig.m_showThreadId)
        {
            pattern = "[Thread %t] " + pattern;
        }

        if (logConfig.m_showTimeStamp)
        {
            pattern = "[%Y-%m-%d %H:%M:%S.%e] " + pattern;
        }
        console_sink->set_pattern(pattern);
        m_ringBufferSink->set_pattern(pattern);

        const spdlog::sinks_init_list sink_list = {console_sink, m_ringBufferSink};
        spdlog::init_thread_pool(8192, 1);  
        m_logger = logConfig.m_async ? std::make_shared<spdlog::async_logger>("spd_logger",
                                                          sink_list.begin(),
                                                          sink_list.end(),
                                                          spdlog::thread_pool(),
                                                          spdlog::async_overflow_policy::block)
                                     : std::make_shared<spdlog::logger>("spd_logger", sink_list);
        m_logger->set_level(spdlog::level::trace);
    }

    void SpdLogSystem::Reset(LogConfig config)
    {
        m_logger.reset();
        InitSpdLogger(config);
    }

    std::vector<std::string> SpdLogSystem::GetLogs()
    {
        return m_ringBufferSink->last_formatted(500);
    }

    SpdLogSystem::~SpdLogSystem()
    {
        m_logger->flush();
        spdlog::drop_all();
    }
}