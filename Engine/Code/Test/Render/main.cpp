#include <gtest/gtest.h>

#include <EASTL/unique_ptr.h>
#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>

using namespace Spark;

//! The allocator warns when the atlas fills, which several tests provoke on purpose. Warn
//! level keeps that out of the report without hiding a real error.
static eastl::unique_ptr<SpdLogSystem> s_logger =
    eastl::make_unique<SpdLogSystem>(LogConfig{true, false, true, false, LogLevel::Error});

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
