#include <gtest/gtest.h>

#include <Log/SpdLogSystem.h>

using namespace Spark;

TEST(LogTest, BasicTest)
{
    LOG_RESET(LogConfig{});
    LOG_INFO("Info log");
    LOG_DEBUG("Debug log");
    LOG_WARN("Warn log");
    LOG_ERROR("Error log");
    LOG_CIRTICAL("Critical log");
    
    
    LOG_RESET(LogConfig{true, false, false, true, LogLevel::Info});
    LOG_INFO("Info log");
    LOG_DEBUG("Debug log");
    LOG_WARN("Warn log");
    LOG_ERROR("Error log");
    LOG_CIRTICAL("Critical log");

    LOG_RESET(LogConfig{true, true, true, false, LogLevel::Debug});
    LOG_INFO("Info log");
    LOG_DEBUG("Debug log");
    LOG_WARN("Warn log");
    LOG_ERROR("Error log");
    LOG_CIRTICAL("Critical log");

    _sleep(2);
}