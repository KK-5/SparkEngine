#include <gtest/gtest.h>

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <EASTL/unique_ptr.h>

#include <Reflection/TypeRegistry.h>
#include <Reflect.h>
#include <Resource/Reflect.h>

using namespace Spark;

static eastl::unique_ptr<SpdLogSystem> s_logger = eastl::make_unique<SpdLogSystem>(LogConfig{true, false, true, false, LogLevel::Info});


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // The descriptor JSON functions resolve their MetaTypes out of the global registry,
    // which SparkEngine::SetUp would normally fill. RegisterAll is once-only, so it lives
    // here rather than in a fixture.
    //
    // Core first, and it is not optional: StandardPBR has Vector4 fields, and an
    // unreflected Vector4 is a class with no reflected members -- which reads and writes
    // as an empty object without a word, leaving the colour at its default.
    TypeRegistry::Register(Spark::Reflect);
    TypeRegistry::Register(Spark::Resource::Reflect);
    TypeRegistry::RegisterAll();

    return RUN_ALL_TESTS();
}