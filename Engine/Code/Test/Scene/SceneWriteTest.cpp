#include <gtest/gtest.h>

#include <string>

#include <nlohmann/json.hpp>

#include <CoreComponents/Name.h>
#include <ECS/StagingContext.h>
#include <Hierarchy/HierarchyComponent.h>

#include <Material/Components.h>
#include <Resource/Material/MaterialState.h>
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
    // No asset identity: the default material and the binding system's synthesized
    // overrides look like this, and neither belongs to the scene.
    const Material::MaterialHandle fallback = materials.CreateEntity();
    materials.Add<Resource::StandardPBR>(fallback);

    const Material::MaterialHandle red = materials.CreateEntity();
    materials.Add<Resource::StandardPBR>(red);
    materials.Add<Material::MaterialAssetRef>(red);

    World world;
    // The editor camera's shape: named, and outside the scene graph.
    world.CreateEntity("EditorCamera");

    const Entity root  = world.CreateEntity("Root");
    const Entity child = world.CreateEntity("Child");
    world.Add<Hierarchy>(root, Hierarchy{NullEntity, child, NullEntity, NullEntity});
    world.Add<Hierarchy>(child, Hierarchy{root, NullEntity, NullEntity, NullEntity});
    world.Add<Material::MaterialComponent>(child, Material::MaterialComponent{red});
    world.Add<SceneTest::Marker>(child);
    world.Add<SceneTest::Scratch>(root, SceneTest::Scratch{7});

    const JsonValue  json  = Write(world, materials);
    const JsonValue& saved = json["contexts"]["world"];

    // The camera is in neither the list nor any segment.
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
    EXPECT_TRUE(material["components"]["MaterialAssetRef"].contains(Key(red)));
}

TEST(SceneWriteTest, EntitiesOutsideTheSceneGraphAreNotWritten)
{
    // An icon entity's shape: created by a system, carrying persistent components, never
    // added to the hierarchy. Nobody had to remember to exclude it.
    World world;
    const Entity icon = world.CreateEntity("Icon");
    world.Add<SceneTest::Marker>(icon);

    const Entity node = world.CreateEntity("Node");
    world.Add<Hierarchy>(node);

    const Materials  materials;
    const JsonValue  json  = Write(world, materials);
    const JsonValue& saved = json["contexts"]["world"];

    EXPECT_EQ(saved["entities"], JsonValue::array({Raw(node)}));
    EXPECT_EQ(Keys(saved["components"]["Name"]), (std::vector<std::string>{Key(node)}));
    EXPECT_FALSE(saved["components"].contains("Marker"));
}

TEST(SceneWriteTest, DestroyedEntitiesAreNotListed)
{
    // Destroying an entity drops the component that made it scene content.
    World world;
    const Entity a = world.CreateEntity();
    const Entity b = world.CreateEntity();
    const Entity c = world.CreateEntity();
    world.Add<Hierarchy>(a);
    world.Add<Hierarchy>(b);
    world.Add<Hierarchy>(c);
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
    first.Add<Hierarchy>(a);
    first.Add<Hierarchy>(b);
    first.Add<Name>(a, "A");
    first.Add<Name>(b, "B");
    first.Add<SceneTest::Marker>(b);

    World second;
    ASSERT_EQ(second.CreateEntity(b), b);
    ASSERT_EQ(second.CreateEntity(a), a);
    second.Add<SceneTest::Marker>(b);
    second.Add<Name>(b, "B");
    second.Add<Hierarchy>(b);
    second.Add<Name>(a, "A");
    second.Add<Hierarchy>(a);

    const Materials materials;
    EXPECT_EQ(Write(first, materials).dump(), Write(second, materials).dump());
}

TEST(SceneWriteTest, MaterialsWithoutAssetIdentityAreNotWritten)
{
    // The default material and a synthesized override: material entities a system holds,
    // rebuilt on its own terms, with no asset behind them.
    Materials materials;
    const Material::MaterialHandle resident = materials.CreateEntity();
    materials.Add<Resource::StandardPBR>(resident);
    materials.Add<Resource::MaterialState>(resident);

    const World      world;
    const JsonValue  json  = Write(world, materials);
    const JsonValue& saved = json["contexts"]["material"];

    EXPECT_EQ(saved["entities"], JsonValue::array());
    EXPECT_TRUE(saved["components"].empty());
}
