#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <ECS/ISystem.h>
#include <Service/Service.h>
#include "ILogSystem.h"

namespace Spark
{
    class SpdLogSystem final: public Service<ILogSystem<SpdLogSystem>>::Handler
    {
    public:
        SpdLogSystem(LogConfig logConfig);
        ~SpdLogSystem();

        template<typename... Args>
        void LogImpl(LogLevel level, Args... args)
        {
            switch (level)
            {
                case LogLevel::Trace:
                    m_logger->trace(std::forward<Args>(args)...);
                    break;
                case LogLevel::Debug:
                    m_logger->debug(std::forward<Args>(args)...);
                    break;
                case LogLevel::Info:
                    m_logger->info(std::forward<Args>(args)...);
                    break;
                case LogLevel::Warn:
                    m_logger->warn(std::forward<Args>(args)...);
                    break;
                case LogLevel::Error:
                    m_logger->error(std::forward<Args>(args)...);
                    break;
                case LogLevel::Critical:
                    m_logger->critical(std::forward<Args>(args)...);
#ifndef NODEBUG
                    assert(false);
#endif
                    break;
                default:
                    break;
            }
        }

        void Reset(LogConfig config) override;
        std::vector<std::string> GetLogs() override;
        
    private:
        void InitSpdLogger(LogConfig logConfig);

        std::shared_ptr<spdlog::logger>                    m_logger;
        std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> m_ringBufferSink;
    };
}

#ifdef NODEBUG
#define LOG_HELPER(LOG_LEVEL, ...) \
    Service<ILogSystem<SpdLogSystem>>::Get()->Log(LOG_LEVEL,  __VA_ARGS__);
#else
#define LOG_HELPER(LOG_LEVEL, ...) \
    (Service<ILogSystem<SpdLogSystem>>::Get() ? Service<ILogSystem<SpdLogSystem>>::Get()->Log(LOG_LEVEL,  __VA_ARGS__) : void(0));
#endif

#ifdef NODEBUG
#define LOG_RESET(...) Service<ILogSystem<SpdLogSystem>>::Get()->Reset( __VA_ARGS__);
#else
#define LOG_RESET(...) (Service<ILogSystem<SpdLogSystem>>::Get() ? Service<ILogSystem<SpdLogSystem>>::Get()->Reset( __VA_ARGS__) : void(0));
#endif

#define LOG_DEBUG(...) LOG_HELPER(LogLevel::Debug, __VA_ARGS__);

#define LOG_INFO(...) LOG_HELPER(LogLevel::Info, __VA_ARGS__);

#define LOG_WARN(...) LOG_HELPER(LogLevel::Warn, __VA_ARGS__);

#define LOG_ERROR(...) LOG_HELPER(LogLevel::Error, __VA_ARGS__);

#define LOG_CIRTICAL(...) LOG_HELPER(LogLevel::Critical, __VA_ARGS__);

#ifdef NODEBUG
#define ASSERT(expression, ...) 
#else
#define ASSERT(expression, ...)                                                                   \
    do                                                                                            \
    {                                                                                             \
        (void)sizeof(expression);                                                                 \
        expression ? (void)0 : LOG_CIRTICAL("{}:{} {}",__FILE__, __LINE__, ##__VA_ARGS__);                   \
    } while (0)
#endif
