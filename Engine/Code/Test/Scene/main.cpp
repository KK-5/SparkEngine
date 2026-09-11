#include <gtest/gtest.h>

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <EASTL/unique_ptr.h>

#include <Reflect.h>
#include <Reflection/TypeRegistry.h>
#include <Material/Reflect.h>
#include <Resource/Reflect.h>

#include "TestComponents.h"

using namespace Spark;

static eastl::unique_ptr<SpdLogSystem> s_logger = eastl::make_unique<SpdLogSystem>(LogConfig{true, false, true, false, LogLevel::Info});

int main(int argc, char **argv)
{
    // TypeRegistry is a global that can only be filled once. The scene writer finds what to
    // write through it, so every module whose components appear below registers here.
    TypeRegistry::Register(Spark::Reflect);
    TypeRegistry::Register(Resource::Reflect);
    TypeRegistry::Register(Material::Reflect);
    TypeRegistry::Register(SceneTest::Reflect);
    TypeRegistry::RegisterAll();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
