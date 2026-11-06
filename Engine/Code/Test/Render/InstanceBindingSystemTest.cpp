#include <gtest/gtest.h>

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBindingSystem.h>
#include <Binding/Material/MaterialBinding.h>
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
};

TEST_F(InstanceBindingSystemTest, EmissionUsesRgbAndStrengthAndIgnoresTheColorAlpha)
{
    const auto material = AddMaterial(Math::Color(0.0f, 0.0f, 0.0f, 1.0f));
    const Entity entity = AddInstance(material);
    EXPECT_FALSE(EncodeInstanceData(world, entity).m_emissive);

    auto& params = materials.Get<Resource::StandardPBR>(material);
    params.m_emissive = Math::Color(0.0f, 2.0f, 0.0f, 0.0f);
    EXPECT_TRUE(EncodeInstanceData(world, entity).m_emissive);

    params.m_emissiveStrength = 0.0f;
    EXPECT_FALSE(EncodeInstanceData(world, entity).m_emissive);
    params.m_emissiveStrength = 3.0f;
    EXPECT_TRUE(EncodeInstanceData(world, entity).m_emissive);
}

TEST_F(InstanceBindingSystemTest, TextureReferencesDoNotReplaceTheEmissiveFactorTest)
{
    const auto material = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    auto& params = materials.Get<Resource::StandardPBR>(material);
    params.m_textures[static_cast<size_t>(Resource::MaterialTexSlot::Emissive)] =
        Resource::AssetId::Of<Resource::ImageAsset>("engine://Image/Emissive.png");
    const Entity entity = AddInstance(material);
    // Candidate collection needs no texture residency or CPU texel inspection.
    EXPECT_TRUE(EncodeInstanceData(world, entity).m_emissive);
    params.m_emissive = Math::Color(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(EncodeInstanceData(world, entity).m_emissive);
}

TEST_F(InstanceBindingSystemTest, MissingGpuMaterialSlotDelaysCandidateCollection)
{
    const auto material = AddMaterial(Math::Color(1.0f, 0.0f, 0.0f, 1.0f), 1.0f, false);
    const Entity entity = AddInstance(material);
    EXPECT_FALSE(EncodeInstanceData(world, entity).m_emissive);
    AddMaterialSlot(material);
    const auto encoded = EncodeInstanceData(world, entity);
    EXPECT_TRUE(encoded.m_emissive);
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(material).Get());
}

TEST_F(InstanceBindingSystemTest, GeometryMustHaveItsSlotAndBeUploaded)
{
    const Entity entity = AddInstance(AddMaterial(Math::Color(1.0f, 0.0f, 0.0f, 1.0f)));
    world.Remove<MeshGeometryReadyTag>(entity);
    EXPECT_FALSE(EncodeInstanceData(world, entity).m_emissive);
    EXPECT_EQ(EncodeInstanceData(world, entity).m_data.m_drawMask, 0u);
    world.Add<MeshGeometryReadyTag>(entity);
    EXPECT_TRUE(EncodeInstanceData(world, entity).m_emissive);
    world.Remove<GeometrySlot>(entity);
    EXPECT_FALSE(EncodeInstanceData(world, entity).m_emissive);
}

TEST_F(InstanceBindingSystemTest, MaterialChangesUseTheSameSlotAsTheInstanceRecord)
{
    const auto light = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    const auto dark = AddMaterial(Math::Color(0.0f, 0.0f, 0.0f, 1.0f));
    const Entity entity = AddInstance(light);
    EXPECT_TRUE(EncodeInstanceData(world, entity).m_emissive);
    world.Get<Material::MaterialComponent>(entity).m_material = dark;
    const auto encoded = EncodeInstanceData(world, entity);
    EXPECT_FALSE(encoded.m_emissive);
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
    EXPECT_FALSE(encoded.m_emissive);
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(overrideMaterial).Get());

    world.Get<Material::StandardPBROverride>(entity).m_emissive = Math::Color(0.0f, 0.0f, 2.0f, 1.0f);
    SyncOverrideMaterials();
    EXPECT_TRUE(EncodeInstanceData(world, entity).m_emissive);

    world.Remove<Material::StandardPBROverride>(entity);
    SyncOverrideMaterials();
    encoded = EncodeInstanceData(world, entity);
    EXPECT_TRUE(encoded.m_emissive);
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(light).Get());
}

TEST_F(InstanceBindingSystemTest, DanglingOrMissingMaterialReferencesUseTheDefault)
{
    const auto material = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    const Entity entity = AddInstance(material);
    materials.DestoryEntity(material);
    auto encoded = EncodeInstanceData(world, entity);
    EXPECT_FALSE(encoded.m_emissive);
    EXPECT_EQ(encoded.m_data.m_materialIndex, materials.Get<MaterialSlot>(defaultMaterial).Get());

    world.Remove<Material::MaterialComponent>(entity);
    materials.Get<Resource::StandardPBR>(defaultMaterial).m_emissive = Math::Color(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(EncodeInstanceData(world, entity).m_emissive);
}

TEST_F(InstanceBindingSystemTest, SharedMaterialsClassifyEachInstanceIndependently)
{
    const auto material = AddMaterial(Math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    const Entity a = AddInstance(material);
    const Entity b = AddInstance(material);
    EXPECT_TRUE(EncodeInstanceData(world, a).m_emissive);
    EXPECT_TRUE(EncodeInstanceData(world, b).m_emissive);
    world.Remove<MeshGeometryReadyTag>(a);
    EXPECT_FALSE(EncodeInstanceData(world, a).m_emissive);
    EXPECT_TRUE(EncodeInstanceData(world, b).m_emissive);
}
