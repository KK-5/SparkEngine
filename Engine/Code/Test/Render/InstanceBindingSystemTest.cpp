#include <gtest/gtest.h>

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBindingSystem.h>
#include <Binding/Material/MaterialBinding.h>
#include <Binding/Scene/SceneBinding.h>
#include <Material/MaterialUtils.h>
#include <Resource/Image/ImageAsset.h>

#include "FakeDevice.h"

using namespace Spark;
using namespace Spark::Render;

// Exercise the same CPU encoding used by the upload path, including effective material
// resolution. The material/geometry slots are real suballocations, without GPU buffers.
class InstanceBindingSystemTest : public ::testing::Test
{
protected:
    WorldContext world;
    Material::MaterialContext materials;
    Ptr<RHI::PhysicalDevice> physicalDevice{new RHI::PhysicalDevice};
    Spark::Test::FakeDevice device;
    Ptr<Suballocator> materialSlots;
    Ptr<Suballocator> geometrySlots;
    Material::MaterialHandle defaultMaterial = Material::NullMaterial;

    void SetUp() override
    {
        ASSERT_EQ(device.Init(*physicalDevice, RHI::DeviceDescriptor{}), RHI::ResultCode::Success);
        WorldExecuteContext::Push(world);
        Material::MaterialExecuteContext::Push(materials);
        materialSlots = new Suballocator(device, 64 * sizeof(MaterialData));
        geometrySlots = new Suballocator(device, 64 * sizeof(GeometryData));
        defaultMaterial = AddMaterial(Math::Color(0.0f, 0.0f, 0.0f, 1.0f));
        materials.Add<Material::DefaultMaterialTag>(defaultMaterial);
    }

    void TearDown() override
    {
        world.Clear();
        materials.Clear();
        geometrySlots.reset();
        materialSlots.reset();
        device.FlushReleaseQueue();
        device.BusDisconnect();
        Material::MaterialExecuteContext::Pop();
        WorldExecuteContext::Pop();
    }

    void AddMaterialSlot(Material::MaterialHandle material)
    {
        materials.Add<MaterialSlot>(material, MaterialSlot{materialSlots->Allocate(sizeof(MaterialData))});
    }

    Material::MaterialHandle AddMaterial(Math::Color emission, float strength = 1.0f, bool ready = true)
    {
        Resource::StandardPBR params;
        params.m_emissive = emission;
        params.m_emissiveStrength = strength;
        const auto material = Material::CreateMaterial(materials, params);
        if (ready)
        {
            AddMaterialSlot(material);
        }
        return material;
    }

    Entity AddInstance(Material::MaterialHandle material)
    {
        const Entity entity = world.CreateEntity();
        world.Add<Transform::WorldTransformMatrix>(entity);
        world.Add<MeshGeometry>(entity);
        world.Add<GeometrySlot>(entity, GeometrySlot{geometrySlots->Allocate(sizeof(GeometryData))});
        world.Add<MeshGeometryReadyTag>(entity);
        world.Add<Material::MaterialComponent>(entity, Material::MaterialComponent{material});
        return entity;
    }

    bool HasEmissiveMaterial(Entity entity)
    {
        const auto material = ResolveInstanceMaterial(world, materials, entity);
        return GetEmissiveMaterial(materials, material) != nullptr;
    }
};

TEST_F(InstanceBindingSystemTest, EmissionUsesRgbAndStrengthAndIgnoresTheColorAlpha)
{
    const auto material = AddMaterial(Math::Color(0.0f, 0.0f, 0.0f, 1.0f));
    const Entity entity = AddInstance(material);
    EXPECT_FALSE(HasEmissiveMaterial(entity));

    auto& params = materials.Get<Resource::StandardPBR>(material);
    params.m_emissive = Math::Color(0.0f, 2.0f, 0.0f, 0.0f);
    EXPECT_TRUE(HasEmissiveMaterial(entity));

    params.m_emissiveStrength = 0.0f;
    EXPECT_FALSE(HasEmissiveMaterial(entity));
    params.m_emissiveStrength = 3.0f;
    EXPECT_TRUE(HasEmissiveMaterial(entity));
}

TEST_F(InstanceBindingSystemTest, TextureReferencesDoNotReplaceTheEmissiveFactorTest)
{
    const auto material = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    auto& params = materials.Get<Resource::StandardPBR>(material);
    params.m_textures[static_cast<size_t>(Resource::MaterialTexSlot::Emissive)] =
        Resource::AssetId::Of<Resource::ImageAsset>("engine://Image/Emissive.png");
    const Entity entity = AddInstance(material);
    EXPECT_TRUE(HasEmissiveMaterial(entity));
    params.m_emissive = Math::Color(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(HasEmissiveMaterial(entity));
}

TEST_F(InstanceBindingSystemTest, MissingGpuMaterialSlotDelaysCandidateCollection)
{
    const auto material = AddMaterial(Math::Color(1.0f, 0.0f, 0.0f, 1.0f), 1.0f, false);
    const Entity entity = AddInstance(material);
    EXPECT_FALSE(HasEmissiveMaterial(entity));
    AddMaterialSlot(material);
    const auto encoded = EncodeInstanceData(world, entity);
    EXPECT_TRUE(HasEmissiveMaterial(entity));
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(material).Get());
}

TEST_F(InstanceBindingSystemTest, GeometryMustHaveItsSlotAndBeUploaded)
{
    const Entity entity = AddInstance(AddMaterial(Math::Color(1.0f, 0.0f, 0.0f, 1.0f)));
    world.Remove<MeshGeometryReadyTag>(entity);
    EXPECT_EQ(EncodeInstanceData(world, entity).m_data.m_drawMask, 0u);
    world.Add<MeshGeometryReadyTag>(entity);
    EXPECT_NE(EncodeInstanceData(world, entity).m_data.m_drawMask, 0u);
    world.Remove<GeometrySlot>(entity);
    EXPECT_EQ(EncodeInstanceData(world, entity).m_data.m_drawMask, 0u);
}

TEST_F(InstanceBindingSystemTest, MaterialChangesUseTheSameSlotAsTheInstanceRecord)
{
    const auto light = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    const auto dark = AddMaterial(Math::Color(0.0f, 0.0f, 0.0f, 1.0f));
    const Entity entity = AddInstance(light);
    EXPECT_TRUE(HasEmissiveMaterial(entity));
    world.Get<Material::MaterialComponent>(entity).m_material = dark;
    const auto encoded = EncodeInstanceData(world, entity);
    EXPECT_FALSE(HasEmissiveMaterial(entity));
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(dark).Get());
}

TEST_F(InstanceBindingSystemTest, OverridesShadowTheReferencedMaterialsEmission)
{
    const auto light = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    const Entity entity = AddInstance(light);
    world.Add<Material::StandardPBROverride>(entity);
    SyncOverrideMaterials();
    const auto overrideMaterial = world.Get<MaterialOverrideRef>(entity).m_material;
    AddMaterialSlot(overrideMaterial);
    auto encoded = EncodeInstanceData(world, entity);
    EXPECT_FALSE(HasEmissiveMaterial(entity));
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(overrideMaterial).Get());

    world.Get<Material::StandardPBROverride>(entity).m_emissive = Math::Color(0.0f, 0.0f, 2.0f, 1.0f);
    SyncOverrideMaterials();
    EXPECT_TRUE(HasEmissiveMaterial(entity));

    world.Remove<Material::StandardPBROverride>(entity);
    SyncOverrideMaterials();
    encoded = EncodeInstanceData(world, entity);
    EXPECT_TRUE(HasEmissiveMaterial(entity));
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(light).Get());
}

TEST_F(InstanceBindingSystemTest, DanglingOrMissingMaterialReferencesUseTheDefault)
{
    const auto material = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    const Entity entity = AddInstance(material);
    materials.DestoryEntity(material);
    auto encoded = EncodeInstanceData(world, entity);
    EXPECT_FALSE(HasEmissiveMaterial(entity));
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(defaultMaterial).Get());

    world.Remove<Material::MaterialComponent>(entity);
    materials.Get<Resource::StandardPBR>(defaultMaterial).m_emissive = Math::Color(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(HasEmissiveMaterial(entity));
}

TEST_F(InstanceBindingSystemTest, SharedMaterialsKeepIndependentLightSlots)
{
    const auto material = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    const Entity a = AddInstance(material);
    const Entity b = AddInstance(material);
    Ptr<Suballocator> lightSlots = new Suballocator(device, 2 * sizeof(LightRecord));
    world.Add<EmissiveLightSlot>(a, EmissiveLightSlot{ { lightSlots->Allocate(sizeof(LightRecord)), {} }, {} });
    world.Add<EmissiveLightSlot>(b, EmissiveLightSlot{ { lightSlots->Allocate(sizeof(LightRecord)), {} }, {} });
    const uint32_t lightSlotA = world.Get<EmissiveLightSlot>(a).m_slot.Get();
    const uint32_t lightSlotB = world.Get<EmissiveLightSlot>(b).m_slot.Get();
    EXPECT_EQ(EncodeInstanceData(world, a).m_data.m_lightSlotId, lightSlotA);
    EXPECT_EQ(EncodeInstanceData(world, b).m_data.m_lightSlotId, lightSlotB);
    world.Remove<EmissiveLightSlot>(a);
    EXPECT_EQ(EncodeInstanceData(world, a).m_data.m_lightSlotId, UINT32_MAX);
    EXPECT_EQ(EncodeInstanceData(world, b).m_data.m_lightSlotId, lightSlotB);
}

TEST_F(InstanceBindingSystemTest, FirstAppearanceHasNoMotionHistoryDespiteTheFallbackMatrix)
{
    const Entity entity = AddInstance(defaultMaterial);
    world.Get<Transform::WorldTransformMatrix>(entity).m_worldMatrix[3][2] = 3.0f;
    const auto encoded = EncodeInstanceData(world, entity);

    EXPECT_EQ(encoded.m_data.m_motionHistoryValid, 0u);
    EXPECT_EQ(encoded.m_data.m_prevModel[3][2], 3.0f);
    EXPECT_FALSE(world.Has<InstanceHistory>(entity));
}

TEST_F(InstanceBindingSystemTest, StaticInstanceWithARealHistoryRemainsValid)
{
    const Entity entity = AddInstance(defaultMaterial);
    world.Add<InstanceHistory>(entity, InstanceHistory{ Math::Matrix4X4Const::IDENTITY, true });

    EXPECT_EQ(EncodeInstanceData(world, entity).m_data.m_motionHistoryValid, 1u);
}

TEST_F(InstanceBindingSystemTest, MovingInstanceUsesThePreviousCorrespondingTransform)
{
    const Entity entity = AddInstance(defaultMaterial);
    Math::Matrix4X4 previous = Math::Matrix4X4Const::IDENTITY;
    previous[3][2] = 4.0f;
    world.Add<InstanceHistory>(entity, InstanceHistory{ previous, true });
    world.Get<Transform::WorldTransformMatrix>(entity).m_worldMatrix[3][2] = 3.0f;
    const auto encoded = EncodeInstanceData(world, entity);

    EXPECT_EQ(encoded.m_data.m_motionHistoryValid, 1u);
    EXPECT_EQ(encoded.m_data.m_prevModel[3][2], 4.0f);
    EXPECT_EQ(encoded.m_data.m_model[3][2], 3.0f);
}

TEST_F(InstanceBindingSystemTest, ExplicitResetRejectsHistoryWithoutDestroyingTheSnapshotDuringEncoding)
{
    const Entity entity = AddInstance(defaultMaterial);
    world.Add<InstanceHistory>(entity, InstanceHistory{ Math::Matrix4X4Const::IDENTITY, true });
    world.Get<Transform::WorldTransformMatrix>(entity).m_worldMatrix[3][2] = 3.0f;
    world.Add<InstanceHistoryResetTag>(entity);
    const auto encoded = EncodeInstanceData(world, entity);

    EXPECT_EQ(encoded.m_data.m_motionHistoryValid, 0u);
    EXPECT_EQ(encoded.m_data.m_prevModel[3][2], 3.0f);
    EXPECT_TRUE(world.Has<InstanceHistoryResetTag>(entity));
    EXPECT_EQ(world.Get<InstanceHistory>(entity).m_model[3][2], 0.0f);
}

TEST_F(InstanceBindingSystemTest, InvalidSnapshotOrUnavailableGeometryCannotSupplyMotionHistory)
{
    const Entity entity = AddInstance(defaultMaterial);
    world.Add<InstanceHistory>(entity, InstanceHistory{ Math::Matrix4X4Const::IDENTITY, false });
    EXPECT_EQ(EncodeInstanceData(world, entity).m_data.m_motionHistoryValid, 0u);

    world.Get<InstanceHistory>(entity).m_valid = true;
    world.Remove<MeshGeometryReadyTag>(entity);
    EXPECT_EQ(EncodeInstanceData(world, entity).m_data.m_motionHistoryValid, 0u);
}
