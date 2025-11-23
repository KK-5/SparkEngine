#include <gtest/gtest.h>

#include <Log/SpdLogSystem.h>
#include <EASTL/unique_ptr.h>

using namespace Spark;

static eastl::unique_ptr<SpdLogSystem> s_logger = eastl::make_unique<SpdLogSystem>(LogConfig{true, false, true, false, LogLevel::Info});


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}