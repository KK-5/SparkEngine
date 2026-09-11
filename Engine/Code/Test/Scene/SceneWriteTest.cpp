#include <gtest/gtest.h>

#include <string>

#include <nlohmann/json.hpp>

#include <CoreComponents/Name.h>
#include <CoreComponents/Tags.h>
#include <ECS/StagingContext.h>
#include <Hierarchy/HierarchyComponent.h>

#include <Material/Components.h>
#include <Resource/Material/StandardPBR.h>
#include <Scene/SceneSerializer.h>

#include "TestComponents.h"

using namespace Spark;

namespace
{
    // Staging contexts on purpose: no events, and they are the case a live-context-only
    // writer would get wrong.
    using World     = StagingContext<Entity>;
    using Materials = StagingContext<Material::MaterialHandle>;

    template<typename E>
    uint32_t Raw(E entity)
    {
        return static_cast<uint32_t>(entity);
    }

    template<typename E>
    std::string Key(E entity)
    {
        return std::to_string(Raw(entity));
    }

    JsonValue Write(const World& world, const Materials& materials)
    {
        JsonValue json;
        EXPECT_TRUE(Scene::WriteScene(world, materials, json));
        return json;
    }

    std::vector<std::string> Keys(const JsonValue& object)
    {
        std::vector<std::string> keys;
        for (const auto& item : object.items())
        {
            keys.push_back(item.key());
        }
        return keys;
    }
}

TEST(SceneWriteTest, ShapeOnDisk)
{
    Materials materials;
    const Material::MaterialHandle fallback = materials.CreateEntity();
    materials.Add<Resource::StandardPBR>(fallback);
    materials.Add<SystemOwnedTag>(fallback);

    const Material::MaterialHandle red = materials.CreateEntity();
    materials.Add<Resource::StandardPBR>(red);

    World world;
    const Entity camera = world.CreateEntity("EditorCamera");
    world.Add<SystemOwnedTag>(camera);

    const Entity root  = world.CreateEntity("Root");
    const Entity child = world.CreateEntity("Child");
    world.Add<Hierarchy>(root, Hierarchy{NullEntity, child, NullEntity, NullEntity});
    world.Add<Hierarchy>(child, Hierarchy{root, NullEntity, NullEntity, NullEntity});
    world.Add<Material::MaterialComponent>(child, Material::MaterialComponent{red});
    world.Add<SceneTest::Marker>(child);
    world.Add<SceneTest::Scratch>(root, SceneTest::Scratch{7});

    const JsonValue  json  = Write(world, materials);
    const JsonValue& saved = json["contexts"]["world"];

    // The system-owned camera is in neither the list nor any segment.
    EXPECT_EQ(saved["entities"], JsonValue::array({Raw(root), Raw(child)}));
    EXPECT_EQ(Keys(saved["components"]["Name"]), (std::vector<std::string>{Key(root), Key(child)}));

    // Sorted by name; Scratch has no Persistent flag and stays out.
    EXPECT_EQ(Keys(saved["components"]),
              (std::vector<std::string>{"Hierarchy", "Marker", "Material", "Name"}));

    const JsonValue& hierarchy = saved["components"]["Hierarchy"];
    EXPECT_TRUE(hierarchy[Key(root)]["Parent"].is_null());
    EXPECT_EQ(hierarchy[Key(root)]["First Child"], Raw(child));
    EXPECT_EQ(hierarchy[Key(child)]["Parent"], Raw(root));

    // A reference across contexts is a key into the other one.
    EXPECT_EQ(saved["components"]["Material"][Key(child)]["Material"], Raw(red));
    EXPECT_EQ(saved["components"]["Marker"][Key(child)], JsonValue::object());

    const JsonValue& material = json["contexts"]["material"];
    EXPECT_EQ(material["entities"], JsonValue::array({Raw(red)}));
    EXPECT_EQ(Keys(material["components"]["StandardPBR"]), (std::vector<std::string>{Key(red)}));
}

TEST(SceneWriteTest, DestroyedEntitiesAreNotListed)
{
    // The entity storage keeps destroyed identifiers for reuse; only the live ones are listed.
    World world;
    const Entity a = world.CreateEntity();
    const Entity b = world.CreateEntity();
    const Entity c = world.CreateEntity();
    world.DestoryEntity(b);

    const Materials materials;
    EXPECT_EQ(Write(world, materials)["contexts"]["world"]["entities"],
              JsonValue::array({Raw(a), Raw(c)}));
}

TEST(SceneWriteTest, SameSceneSameText)
{
    // The same scene built in opposite orders: storages are created in a different order, and
    // the entities inside each come out in a different order too. The text must not.
    World first;
    const Entity a = first.CreateEntity();
    const Entity b = first.CreateEntity();
    first.Add<Name>(a, "A");
    first.Add<Name>(b, "B");
    first.Add<SceneTest::Marker>(b);

    World second;
    ASSERT_EQ(second.CreateEntity(b), b);
    ASSERT_EQ(second.CreateEntity(a), a);
    second.Add<SceneTest::Marker>(b);
    second.Add<Name>(b, "B");
    second.Add<Name>(a, "A");

    const Materials materials;
    EXPECT_EQ(Write(first, materials).dump(), Write(second, materials).dump());
}
