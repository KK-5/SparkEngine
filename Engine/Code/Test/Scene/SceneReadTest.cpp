#include <gtest/gtest.h>

#include <string>

#include <nlohmann/json.hpp>

#include <CoreComponents/Name.h>
#include <ECS/Common.h>
#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>
#include <Hierarchy/HierarchyComponent.h>
#include <Hierarchy/HierarchyManager.h>
#include <Service/Service.h>

#include <Material/Components.h>
#include <Material/MaterialContext.h>
#include <Resource/Material/MaterialState.h>
#include <Resource/Material/StandardPBR.h>
#include <Scene/SceneSerializer.h>

#include "TestComponents.h"

using namespace Spark;

namespace
{
    //! A live pair of contexts with the hierarchy manager watching, which is what a scene is
    //! read into: the tree is rebuilt by the batch event, not by the reader.
    class SceneReadTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            WorldExecuteContext::Push(world);
            Material::MaterialExecuteContext::Push(materials);
            hierarchy = CreateSystem<HierarchyManager>();
            hierarchy->Init();
        }

        void TearDown() override
        {
            hierarchy.reset();
            Material::MaterialExecuteContext::Pop();
            WorldExecuteContext::Pop();
        }

        //! A material entity the way Resolve makes one: parameters, state and an asset identity.
        Material::MaterialHandle CreateMaterial(uint32_t assetSeed)
        {
            const Material::MaterialHandle handle = materials.CreateEntity();
            materials.Add<Resource::StandardPBR>(handle);
            materials.Add<Resource::MaterialState>(handle);
            materials.Add<Material::MaterialAssetRef>(handle);
            materials.Get<Resource::StandardPBR>(handle).m_baseColor.x = static_cast<float>(assetSeed);
            return handle;
        }

        JsonValue Write()
        {
            JsonValue json;
            EXPECT_TRUE(Scene::WriteScene(world, materials, json));
            return json;
        }

        SystemUniquePtr<HierarchyManager> hierarchy;
        WorldContext                      world;
        Material::MaterialContext         materials;
    };

    Entity Child(const WorldContext& world, Entity parent)
    {
        return world.Get<Hierarchy>(parent).firstChild;
    }
}

TEST_F(SceneReadTest, AScenePutBackIsTheSameScene)
{
    auto* tree = Service<IHierarchy>::Get();
    ASSERT_TRUE(tree);

    const Material::MaterialHandle red = CreateMaterial(1);

    const Entity root  = world.CreateEntity("Root");
    const Entity child = world.CreateEntity("Child");
    tree->AddEntity(root);
    tree->SetParent(child, root);
    world.Add<Material::MaterialComponent>(child, Material::MaterialComponent{red});
    world.Add<SceneTest::Marker>(child);

    const JsonValue before = Write();

    Scene::ClearScene(world, materials);
    ASSERT_FALSE(world.Valid(root));
    ASSERT_FALSE(world.Valid(child));

    ASSERT_TRUE(Scene::ReadScene(before, world, materials));

    // Same text, so the same entities carry the same components under the same identifiers.
    EXPECT_EQ(Write().dump(), before.dump());

    // The tree was rebuilt by the batch event, not written into the file as a tag.
    EXPECT_TRUE(world.Has<HierarchyRootTag>(root));
    EXPECT_FALSE(world.Has<HierarchyRootTag>(child));
    EXPECT_EQ(Child(world, root), child);
}

TEST_F(SceneReadTest, AMaterialThatMovedTakesItsReferencesWithIt)
{
    auto* tree = Service<IHierarchy>::Get();
    ASSERT_TRUE(tree);

    const Material::MaterialHandle red = CreateMaterial(1);
    const Entity object = world.CreateEntity("Object");
    tree->AddEntity(object);
    world.Add<Material::MaterialComponent>(object, Material::MaterialComponent{red});

    const JsonValue file = Write();

    Scene::ClearScene(world, materials);

    // Something a system owns now sits on the identifier the file's material wants, the way a
    // resident default material would.
    const Material::MaterialHandle resident = materials.CreateEntity();
    materials.Add<Resource::StandardPBR>(resident);
    // The slot the file names is taken -- by a recycled identifier, so not even the same value.
    ASSERT_NE(materials.EntityAt(red), Material::NullMaterial);
    ASSERT_NE(resident, red);

    ASSERT_TRUE(Scene::ReadScene(file, world, materials));

    const auto loaded = world.GetView<Material::MaterialComponent>();
    ASSERT_EQ(loaded.begin() != loaded.end(), true);
    const Material::MaterialHandle landed = world.Get<Material::MaterialComponent>(*loaded.begin()).m_material;

    EXPECT_NE(landed, red);
    EXPECT_NE(landed, resident);
    EXPECT_TRUE(materials.Has<Material::MaterialAssetRef>(landed));
}

TEST_F(SceneReadTest, AnUnknownSegmentIsSkipped)
{
    JsonValue file = JsonValue::object();
    file["contexts"]["world"]["entities"] = JsonValue::array({1u});
    file["contexts"]["world"]["components"]["Hierarchy"]["1"] = {
        {"Parent", nullptr}, {"First Child", nullptr},
        {"Prev Sibling", nullptr}, {"Next Sibling", nullptr}};
    file["contexts"]["world"]["components"]["SomethingRemoved"]["1"] = JsonValue::object();

    EXPECT_TRUE(Scene::ReadScene(file, world, materials));
    EXPECT_TRUE(world.Has<Hierarchy>(static_cast<Entity>(1)));
}

TEST_F(SceneReadTest, ABadFileChangesNothing)
{
    const Entity resident = world.CreateEntity("Resident");

    JsonValue file = JsonValue::object();
    file["contexts"]["world"]["entities"] = JsonValue::array({7u});
    // The component names an entity the list never declared.
    file["contexts"]["world"]["components"]["Name"]["8"] = {{"Value", "Ghost"}};

    EXPECT_FALSE(Scene::ReadScene(file, world, materials));

    size_t live = 0;
    for (Entity entity : world.GetView<Name>())
    {
        ++live;
        EXPECT_EQ(entity, resident);
    }
    EXPECT_EQ(live, 1u);
    EXPECT_FALSE(world.Valid(static_cast<Entity>(7)));
}

TEST_F(SceneReadTest, ClearingTakesTheSceneAndLeavesTheRest)
{
    auto* tree = Service<IHierarchy>::Get();
    ASSERT_TRUE(tree);

    const Entity inScene = world.CreateEntity("InScene");
    tree->AddEntity(inScene);
    const Entity icon = world.CreateEntity("Icon");          // never in the graph

    const Material::MaterialHandle asset    = CreateMaterial(1);
    const Material::MaterialHandle resident = materials.CreateEntity();
    materials.Add<Resource::StandardPBR>(resident);           // no asset identity

    Scene::ClearScene(world, materials);

    EXPECT_FALSE(world.Valid(inScene));
    EXPECT_TRUE(world.Valid(icon));
    EXPECT_FALSE(materials.Valid(asset));
    EXPECT_TRUE(materials.Valid(resident));
}
