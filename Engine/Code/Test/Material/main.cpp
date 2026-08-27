#include <gtest/gtest.h>

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <EASTL/unique_ptr.h>

#include <Reflect.h>
#include <Reflection/TypeRegistry.h>
#include <Material/Reflect.h>
#include <Resource/Reflect.h>

using namespace Spark;

static eastl::unique_ptr<SpdLogSystem> s_logger = eastl::make_unique<SpdLogSystem>(LogConfig{true, false, true, false, LogLevel::Info});

int main(int argc, char **argv)
{
    // TypeRegistry is a global that can only be filled once, so this belongs here rather
    // than in a fixture. Core brings the math types, Resource brings AssetId and its
    // JsonOperation -- MaterialParams needs both.
    TypeRegistry::Register(Spark::Reflect);
    TypeRegistry::Register(Resource::Reflect);
    TypeRegistry::Register(Material::Reflect);
    TypeRegistry::RegisterAll();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
