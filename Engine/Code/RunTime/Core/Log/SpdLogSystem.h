#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <ECS/ISystem.h>
#include <Service/Service.h>
#include "ILogSystem.h"

namespace Spark
{
    class SpdLogSystem final : public Service<ILogSystem>::Handler
    {
    public:
        SpdLogSystem(LogConfig logConfig);
        ~SpdLogSystem();

        // ILogSystem
        void LogV(LogLevel level, fmt::string_view fmt, fmt::format_args args) override;
        void Reset(LogConfig config) override;
        std::vector<std::string> GetLogs() override;

    private:
        void InitSpdLogger(LogConfig logConfig);

        std::shared_ptr<spdlog::logger>                    m_logger;
        std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> m_ringBufferSink;
    };
}
