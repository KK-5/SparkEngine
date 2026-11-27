#include "SceneBindingSystem.h"

#include <string.h>

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <CoreComponents/Tags.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>

#include <RHI/RHIInterface.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/HardwareQueue.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>


#include <Shader/ShaderBindingsUtils.h>
#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Binding/SharedGroupLayouts.h>
#include <Binding/ShaderUsage.h>
#include <Binding/SharedGroupValues.h>
#include <Binding/Geometry/GeometryBindingSystem.h>
#include <Binding/Material/MaterialBinding.h>
#include <Binding/View/ViewBinding.h>
#include <Pass/ComputePass.h>
#include <Pass/CopyPass.h>
#include <RenderGraph/PassScopes.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>

#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>

#include <ECS/Common.h>

#include <Light/Components.h>
#include <Skybox/Components.h>
#include <Scene/SceneTextureComponents.h>
#include <Transform/Components.h>

#include <View/View.h>
#include <View/ViewTags.h>
#include <View/ViewComponents.h>
#include <View/ShadowMapLayout.h>
#include <View/ShadowMaskLayout.h>

#include <Pass/Component/RHIComponents.h>

#include "RenderGraph/RenderGraphUtils.h"

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t LightCapacity = 1u << 16;

        uint32_t LightWord(float value)
        {
            uint32_t word;
            memcpy(&word, &value, sizeof(word));
            return word;
        }

        eastl::array<uint32_t, 4> PackLightVector(const Math::Vector3& v, uint32_t w)
        {
            return { LightWord(v.x), LightWord(v.y), LightWord(v.z), w };
        }

        template<typename Tag>
        RHI::RHIHandle FindLookupTable(RHI::RHIContext& context)
        {
            RHI::RHIHandle table = RHI::NullHandle;
            context.GetView<Tag>(Exclude<DeadTag>).each([&](RHI::RHIHandle entity) { table = entity; });
            return table;
        }

        //! Copies `records` into this frame's buffer of `staging` in one go and publishes a view
        //! of the whole buffer as `input`, so each buffer gets one view whatever the count. Room
        //! for one record at least, so the view is never empty. The buffer's slots rotate, so
        //! `taggedSlot` follows this frame's, and the usage is refreshed on it.
        template<typename BufferTag, typename Record>
        void WriteAndBind(RHI::RHIContext& rhiCtx, StagingBuffer& staging, const eastl::vector<Record>& records,
            const char* input, RHI::RHIHandle& taggedSlot)
        {
            const uint64_t byteCount = eastl::max<uint64_t>(records.size(), 1) * sizeof(Record);
            staging.Begin(byteCount);
            const StagingBuffer::Allocation allocation = staging.Allocate(byteCount);
            RHI::Buffer* buffer = staging.GetBuffer();
            if (!allocation.m_data || !buffer)
            {
                return;
            }
            if (!records.empty())
            {
                memcpy(allocation.m_data, records.data(), records.size() * sizeof(Record));
            }

            const RHI::BufferViewDescriptor viewDesc = RHI::BufferViewDescriptor::CreateStructured(
                0, static_cast<uint32_t>(buffer->GetDescriptor().m_byteCount / sizeof(Record)), sizeof(Record));

            const RHI::RHIHandle resource = staging.GetResource();
            if (taggedSlot != resource)
            {
                if (taggedSlot != RHI::NullHandle && rhiCtx.Valid(taggedSlot))
                {
                    rhiCtx.Remove<BufferTag>(taggedSlot);
                }
                rhiCtx.Add<BufferTag>(resource);
                taggedSlot = resource;
            }

            rhiCtx.AddOrReplace<ShaderBufferUsage>(resource, ShaderBufferUsage{
                ShaderInputBinding{ kSceneSpaceId, RHI::InputName(input), 0 }, viewDesc });
        }
    }

    void SceneBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        // The group belongs to the render graph (SharedBindingPool), built from the declaration
        // in SharedGroupLayouts.h. What no resource carries - the sampler and the constants - is
        // refilled each frame and written into that group at compile.
        m_values = rhiCtx.CreateEntity();
        SharedGroupValues values;
        values.m_spaceId = kSceneSpaceId;
        rhiCtx.Add<SharedGroupValues>(m_values, values);
        rhiCtx.Add<SceneLightSlotCount>(m_values, SceneLightSlotCount{});

        RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        {
            DeviceArray::Descriptor lightDesc;
            lightDesc.m_name        = ObjectName(SharedInput::Lights);
            lightDesc.m_elementSize = sizeof(LightRecord);
            lightDesc.m_capacity    = LightCapacity;
            lightDesc.m_inputName   = RHI::InputName(SharedInput::Lights);
            lightDesc.m_spaceId     = kSceneSpaceId;
            if (m_lightBuffer.Init(rhiCtx, device, lightDesc))
            {
                rhiCtx.Add<LightBufferTag>(m_lightBuffer.GetResource());
            }
        }
        {
            StagingBuffer::Descriptor shadowDesc;
            shadowDesc.m_name            = ObjectName(SharedInput::ShadowViews);
            shadowDesc.m_bindFlags       = RHI::BufferBindFlags::ShaderRead;
            shadowDesc.m_initialCapacity = uint64_t(kShadowMapCount) * sizeof(ShadowViewData);
            m_shadowViewBuffer.Init(device, shadowDesc);
        }
        {
            StagingBuffer::Descriptor viewDesc;
            viewDesc.m_name      = ObjectName(SharedInput::ViewShadowMaps);
            viewDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead;
            m_viewShadowMapBuffer.Init(device, viewDesc);
        }
    }

    void SceneBindingSystem::SetUp(PassContext& passContext)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");
        const auto assetId = assetManager->MakeAssetId("engine://Shaders/ElementUpdate/ElementUpdate.hlsl");
        const auto updateShader = assetId.IsValid()
            ? assetManager->LoadAsset<Resource::ShaderAsset>(assetId) : Ptr<Resource::ShaderAsset>{};
        if (!updateShader)
        {
            LOG_ERROR("[SceneBindingSystem] Failed to load the light update shader.");
            return;
        }

        SPARK_COPY_PASS(passContext, "SceneLightPageClear")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .Build([this](CopyPassScopes& p)
            {
                eastl::vector<DeviceArray::ByteRange> ranges;
                m_lightBuffer.GetInitializationRanges(ranges);
                if (ranges.empty())
                {
                    return;
                }

                const RHI::AttachmentId lightsName("SceneLights");
                p.Import(lightsName, m_lightBuffer.GetResource());
                auto scope = p.Scope();
                const auto destination = scope.FillDestination(lightsName);
                for (const auto& range : ranges)
                {
                    scope.FillBuffer(destination, range.m_byteOffset, range.m_byteCount, 0u);
                }
                scope.Close();
            })
            .Finalize();
        SPARK_COMPUTE_PASS(passContext, "SceneLightUpdate")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(updateShader)
            .Build([this](ComputePassScopes& p)
            {
                const ElementUpdateBuffer& updates = m_lightBuffer.GetUpdates();
                const uint32_t updateCount = updates.GetUpdateCount();
                if (updateCount == 0)
                {
                    return;
                }

                const RHI::AttachmentId updatesName("SceneLightUpdates");
                const RHI::AttachmentId lightsName("SceneLights");
                p.Import(updatesName, updates.GetResource());
                p.Import(lightsName, m_lightBuffer.GetResource());

                auto scope = p.Scope();
                scope.ReadBuffer(updatesName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(updates.GetByteCount())))
                    .Bind(RHI::InputName("g_Updates"));
                scope.ReadWriteBuffer(lightsName)
                    .View(RHI::BufferViewDescriptor::CreateRaw(0, static_cast<uint32_t>(m_lightBuffer.GetByteCount())))
                    .Bind(RHI::InputName("g_Elements"));
                const uint32_t elementWords = updates.GetElementSize() / sizeof(uint32_t);
                scope.Constant(RHI::InputName("updateCount"), updateCount);
                scope.Constant(RHI::InputName("elementWords"), elementWords);
                scope.Constant(RHI::InputName("indicesOffset"), static_cast<uint32_t>(updates.GetIndicesOffset()));
                scope.Constant(RHI::InputName("elementsOffset"), static_cast<uint32_t>(updates.GetElementsOffset()));
                scope.Dispatch(updateCount * elementWords);
                scope.Close();
            })
            .Finalize();
    }

    void SceneBindingSystem::PackShadowViews(WorldContext& world, RHI::RHIContext& rhiCtx)
    {
        m_shadowViews.assign(kShadowMapCount, ShadowViewData{});
        m_viewShadowMaps.clear();

        world.GetView<ShadowMaps, ShadowViewRefs, Light::LightRenderData>(Exclude<DeadTag>).each(
            [&](const ShadowMaps& maps, const ShadowViewRefs& refs, const Light::LightRenderData& rd)
        {
            const float extent = ShadowMapExtent(maps.m_level);
            const float texels = static_cast<float>(ShadowResolution(maps.m_level));
            for (uint32_t i = 0; i < refs.m_views.size(); ++i)
            {
                const RHI::RHIHandle viewHandle = refs.m_views[i].Get();
                const uint32_t       index      = maps.First() + i;

                if (const auto* row = rhiCtx.TryGet<ViewIndex>(viewHandle))
                {
                    if (row->m_index >= m_viewShadowMaps.size())
                    {
                        m_viewShadowMaps.resize(row->m_index + 1, 0);
                    }
                    m_viewShadowMaps[row->m_index] = index;
                }

                // Not committed this frame: its row stays a hole, read as unshadowed.
                if (rhiCtx.Has<ViewInactiveTag>(viewHandle))
                {
                    continue;
                }

                const View&     view = rhiCtx.Get<View>(viewHandle);
                ShadowViewData& d    = m_shadowViews[index];
                d.m_worldToShadowUV  = ShadowClipToUV(extent) * view.GetWorldToClip();
                d.m_uvMax            = extent;

                // [0][0] is 1/tan(halfFov) under a perspective projection and 1/halfExtent under
                // an orthographic one, so this one expression is the world width NDC [-1,1]
                // spans in either — divided by the texels it lands on.
                const float scaleX     = view.m_viewToClip[0][0];
                d.m_texelWorldSizePerW = scaleX != 0.0f ? 2.0f / (scaleX * texels) : 0.0f;

                d.m_depthBias          = rd.m_shadowBias;
                d.m_normalOffsetTexels = rd.m_shadowNormalOffsetTexels;
                d.m_pcfRadiusTexels    =
                    0.5f * static_cast<float>(Light::ShadowFilterFootprint(rd.m_shadowFilterWidth));
            }
        });
    }

    void SceneBindingSystem::AllocateLightSlots(WorldContext& world)
    {
        const auto liveLights = world.GetView<Light::LightRenderData, Light::LightComponent,
            Transform::WorldTransformMatrix>(Exclude<DeadTag>);
        for (auto [entity, slot] : world.GetView<LightSlot>().each())
        {
            if (!liveLights.contains(entity))
            {
                world.Add<RemovedLightSlot>(entity, RemovedLightSlot{ slot.Get(), slot.m_record.m_current });
            }
        }
        for (Entity entity : world.GetView<Light::LightRenderData, Light::LightComponent,
            Transform::WorldTransformMatrix>(Exclude<LightSlot, DeadTag>))
        {
            Suballocation range = m_lightBuffer.Allocate();
            if (!range.IsValid())
            {
                LOG_ERROR("[SceneBindingSystem] All {} light slots are taken.", LightCapacity);
                return;
            }
            world.Add<LightSlot>(entity, LightSlot{ eastl::move(range), {} });
        }
    }

    void SceneBindingSystem::PrepareEmissiveLights(WorldContext& world)
    {
        auto* materials = Material::MaterialExecuteContext::Current();
        if (!materials)
        {
            return;
        }

        constexpr size_t emissiveTextureSlot = static_cast<size_t>(Resource::MaterialTexSlot::Emissive);
        const auto readyMeshes = world.GetView<Transform::WorldTransformMatrix, MeshGeometry,
            GeometrySlot, MeshGeometryReadyTag>(Exclude<DeadTag>);
        for (Entity entity : readyMeshes)
        {
            const auto material = ResolveInstanceMaterial(world, *materials, entity);
            const auto* params = GetEmissiveMaterial(*materials, material);
            if (!params)
            {
                continue;
            }
            const GeometryData geometry = EncodeGeometryData(world.Get<MeshGeometry>(entity));
            const auto& texture = params->m_textures[emissiveTextureSlot];
            const uint32_t textureIndex = materials->Get<MaterialSlot>(material).m_emissiveTextureIndex;
            if (geometry.m_triangleCount == 0 || (texture.IsValid() && textureIndex == InvalidTextureIndex))
            {
                continue;
            }

            LightData current;
            current.m_type = EmissiveMeshLightType;
            current.m_color = Math::Vector3(params->m_emissive);
            current.m_intensity = params->m_emissiveStrength;
            const auto& model = world.Get<Transform::WorldTransformMatrix>(entity).m_worldMatrix;
            for (int row = 0; row < 3; ++row)
            {
                const Math::Vector4 v = Math::Row(model, row);
                current.m_typeData[row] = PackLightVector(Math::Vector3(v), LightWord(v.w));
            }
            current.m_typeData[3] = { geometry.m_firstIndex, static_cast<uint32_t>(geometry.m_vertexOffset),
                geometry.m_indexByteStride, geometry.m_triangleCount };
            current.m_typeDataTail = { 0u, textureIndex, LightWord(geometry.m_localSurfaceArea) };

            auto* emitter = world.TryGet<EmissiveLightSlot>(entity);
            if (emitter)
            {
                const auto& previous = emitter->m_slot.m_record.m_current;
                if (previous.m_typeData[3] != current.m_typeData[3] || emitter->m_emissiveTexture != texture
                    || previous.m_typeDataTail[1] != textureIndex)
                {
                    world.Add<RemovedEmissiveLightSlot>(entity,
                        RemovedEmissiveLightSlot{ { emitter->m_slot.Get(), previous } });
                    world.Remove<EmissiveLightSlot>(entity);
                    emitter = nullptr;
                }
            }
            if (!emitter)
            {
                Suballocation range = m_lightBuffer.Allocate();
                if (!range.IsValid())
                {
                    continue;
                }
                emitter = &world.Add<EmissiveLightSlot>(entity, EmissiveLightSlot{ { eastl::move(range), {} }, texture });
            }
            auto& record = emitter->m_slot.m_record;
            record.m_previous = record.m_current;
            record.m_current = current;
            emitter->m_activeThisFrame = true;
        }
    }

    uint32_t SceneBindingSystem::UpdateLights(WorldContext& world)
    {
        if (m_lightBuffer.GetResource() == RHI::NullHandle)
        {
            return 0;
        }

        AllocateLightSlots(world);
        PrepareEmissiveLights(world);
        m_lightBuffer.BeginUpdate();
        for (auto [entity, slot] : world.GetView<RemovedLightSlot>().each())
        {
            const LightRecord record{ {}, slot.m_previous };
            m_lightBuffer.Set(slot.m_index, record);
            world.Remove<LightSlot>(entity);
        }
        uint32_t slotCount = 0;
        world.GetView<LightSlot, Light::LightRenderData>().each(
            [&](Entity entity, LightSlot& slot, const Light::LightRenderData& rd)
        {
            auto& record = slot.m_record;
            record.m_previous = record.m_current;
            LightData& d = record.m_current;
            d.m_color = rd.m_color;
            d.m_type = static_cast<uint32_t>(rd.m_type) + 1u;
            d.m_intensity = rd.m_intensity;
            const auto* maps = world.TryGet<ShadowMaps>(entity);
            const auto* refs = world.TryGet<ShadowViewRefs>(entity);
            const auto* maskSlot = world.TryGet<ShadowMaskSlot>(entity);
            d.m_typeData[0] = PackLightVector(rd.m_worldDirection, LightWord(rd.m_directionalConeHeight));
            d.m_typeData[1] = PackLightVector(rd.m_worldPosition, LightWord(rd.m_range > 0.0f ? 1.0f / rd.m_range : 0.0f));
            d.m_typeData[2] = { LightWord(rd.m_cosInner), LightWord(rd.m_cosOuter),
                maps ? static_cast<uint32_t>(maps->First()) : UINT32_MAX,
                refs ? static_cast<uint32_t>(refs->m_views.size()) : 1u };
            d.m_typeData[3] = PackLightVector(rd.m_worldRight, maskSlot ? maskSlot->m_index : UINT32_MAX);
            d.m_typeDataTail = { LightWord(rd.m_halfWidth), LightWord(rd.m_halfHeight), rd.m_castShadow ? 1u : 0u };
            const uint32_t index = slot.Get();
            m_lightBuffer.Set(index, record);
            slotCount = eastl::max(slotCount, index + 1u);
        });
        for (auto [entity, emitter] : world.GetView<EmissiveLightSlot>().each())
        {
            const uint32_t index = emitter.m_slot.Get();
            if (!emitter.m_activeThisFrame)
            {
                world.Add<RemovedEmissiveLightSlot>(entity,
                    RemovedEmissiveLightSlot{ { index, emitter.m_slot.m_record.m_current } });
                continue;
            }
            emitter.m_activeThisFrame = false;
            m_lightBuffer.Set(index, emitter.m_slot.m_record);
            slotCount = eastl::max(slotCount, index + 1u);
        }
        for (auto [entity, removed] : world.GetView<RemovedEmissiveLightSlot>().each())
        {
            const auto& slot = removed.m_slot;
            const LightRecord record{ {}, slot.m_previous };
            m_lightBuffer.Set(slot.m_index, record);
            // A replacement on the same entity already holds a different slot.
            if (const auto* emitter = world.TryGet<EmissiveLightSlot>(entity);
                emitter && emitter->m_slot.Get() == slot.m_index)
            {
                world.Remove<EmissiveLightSlot>(entity);
            }
        }

        m_lightBuffer.EndUpdate();
        world.Clear<RemovedLightSlot, RemovedEmissiveLightSlot>();
        return slotCount;
    }

    SceneBindingSystem::EnvironmentBinding SceneBindingSystem::BindEnvironmentIBL()
    {
        EnvironmentBinding result;

        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || m_values == RHI::NullHandle)
        {
            return result;
        }

        Entity active = NullEntity;
        world->GetView<Skybox::ActiveSkyboxTag>(Exclude<DeadTag>).each(
            [&](Entity entity) { active = entity; });
        if (active == NullEntity)
        {
            return result;
        }

        // Straight off the user-facing component: no GPU resource, so it applies from the
        // first frame and costs no rebuild when the slider moves.
        if (const auto* comp = world->TryGet<Skybox::SkyboxComponent>(active))
        {
            result.m_intensity = comp->m_intensity;
        }

        const auto* gpu = world->TryGet<SkyboxGPUComponent>(active);
        if (!gpu)
        {
            return result;
        }
        const RHI::RHIHandle irradiance  = gpu->m_irradiance.Get();
        const RHI::RHIHandle prefiltered = gpu->m_prefiltered.Get();
        const auto brdfLut = FindLookupTable<BRDFLutTag>(*rhiCtx);

        // Published on the images themselves, as soon as they exist: this system is where the
        // scene's pieces, owned by several others, are gathered and given their shader usage.
        // The view is this group's choice, so it is made here, not by the owners. Publishing
        // does not wait for the upload: a pass imports an input the moment it is there, so a
        // publication that waited for readiness would race it.
        const RHI::ImageViewDescriptor cubeView = RHI::ImageViewDescriptor::CreateCubemap();
        if (irradiance != RHI::NullHandle)
        {
            rhiCtx->AddOrReplace<ShaderImageUsage>(irradiance, ShaderImageUsage{
                ShaderInputBinding{ kSceneSpaceId, RHI::InputName(SharedInput::IrradianceCube), 0 }, cubeView });
        }
        if (prefiltered != RHI::NullHandle)
        {
            rhiCtx->AddOrReplace<ShaderImageUsage>(prefiltered, ShaderImageUsage{
                ShaderInputBinding{ kSceneSpaceId, RHI::InputName(SharedInput::PrefilteredCube), 0 }, cubeView });
        }

        // All or nothing for the constant: a partially ready set must not be sampled, and the
        // mip count is what the shader gates on. The LUT joins the same gate -- without it the
        // specular term has no F0 scale/bias to apply.
        if (!RHI::IsResourceReady(*rhiCtx, irradiance)
            || !RHI::IsResourceReady(*rhiCtx, prefiltered)
            || !RHI::IsResourceReady(*rhiCtx, brdfLut))
        {
            return result;
        }

        const auto* prefilteredImage = rhiCtx->TryGet<RHI::Components::Image>(prefiltered);
        if (prefilteredImage == nullptr)
        {
            return result;
        }

        // From the live image, not a bake-time constant, so a re-bake at a different mip
        // count needs no change here.
        result.m_prefilteredMipCount = prefilteredImage->m_image->GetDescriptor().m_mipLevels;
        return result;
    }

    bool SceneBindingSystem::BindAreaLightLuts(RHI::RHIContext& rhiCtx)
    {
        const auto ltc1 = FindLookupTable<LTC1Tag>(rhiCtx);
        const auto ltc2 = FindLookupTable<LTC2Tag>(rhiCtx);

        // Published as soon as the tables exist, for the reason BindEnvironmentIBL gives.
        if (ltc1 != RHI::NullHandle)
        {
            rhiCtx.AddOrReplace<ShaderImageUsage>(ltc1, ShaderImageUsage{
                ShaderInputBinding{ kSceneSpaceId, RHI::InputName(SharedInput::LTC1), 0 }, RHI::ImageViewDescriptor{} });
        }
        if (ltc2 != RHI::NullHandle)
        {
            rhiCtx.AddOrReplace<ShaderImageUsage>(ltc2, ShaderImageUsage{
                ShaderInputBinding{ kSceneSpaceId, RHI::InputName(SharedInput::LTC2), 0 }, RHI::ImageViewDescriptor{} });
        }

        // Both or neither for the flag, for the reason BindEnvironmentIBL gives.
        return RHI::IsResourceReady(rhiCtx, ltc1) && RHI::IsResourceReady(rhiCtx, ltc2);
    }

    void SceneBindingSystem::Update(const FrameTime& time)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || m_values == RHI::NullHandle)
        {
            return;
        }

        PackShadowViews(*world, *rhiCtx);
        WriteAndBind<ShadowViewBufferTag>(*rhiCtx, m_shadowViewBuffer, m_shadowViews, SharedInput::ShadowViews, m_shadowViewSlot);
        WriteAndBind<ViewShadowMapBufferTag>(*rhiCtx, m_viewShadowMapBuffer, m_viewShadowMaps, SharedInput::ViewShadowMaps, m_viewShadowMapSlot);
        const uint32_t lightSlotCount = UpdateLights(*world);
        rhiCtx->Get<SceneLightSlotCount>(m_values).m_count = lightSlotCount;

        // The BRDF LUT is its own table, not part of the sky's set: it is published whether or
        // not there is a skybox, since a pass reads it as soon as the table is there.
        const auto brdfLut = FindLookupTable<BRDFLutTag>(*rhiCtx);
        if (brdfLut != RHI::NullHandle)
        {
            rhiCtx->AddOrReplace<ShaderImageUsage>(brdfLut, ShaderImageUsage{
                ShaderInputBinding{ kSceneSpaceId, RHI::InputName(SharedInput::BRDFLut), 0 }, RHI::ImageViewDescriptor{} });
        }

        // What no resource carries, refilled every frame: the compiler writes it into the group.
        auto* values = rhiCtx->TryGet<SharedGroupValues>(m_values);
        ASSERT(values != nullptr, "[SceneBindingSystem] The space0 group has no values component.");
        if (values == nullptr)
        {
            return;
        }
        values->m_samplers.clear();
        values->m_constants.clear();

        // A per-scene constant, not a per-view one: every layer is as wide, whatever the
        // resolution of the shadow map in it.
        PushSharedConstant(*values, RHI::InputName(SharedInput::ShadowTexelSize), 1.0f / static_cast<float>(kShadowMapResolution));

        // g_LightSlotCount rides the SceneConstants cbuffer in the same SRG. Written every frame
        // so the CBV is always compiled: its bind is unguarded in CommandList, unlike the SRV
        // table's, and a missing address trips a hard assert. The shader loop is empty while
        // count is 0.
        PushSharedConstant(*values, RHI::InputName(SharedInput::LightSlotCount), lightSlotCount);

        // Same unconditional-write rule as g_LightSlotCount; 0 mips is also the shader's gate,
        // since the cubes share g_Lights' table and "unbound" there means stale, not null.
        const EnvironmentBinding env = BindEnvironmentIBL();
        PushSharedConstant(*values, RHI::InputName(SharedInput::PrefilteredMips), env.m_prefilteredMipCount);
        PushSharedConstant(*values, RHI::InputName(SharedInput::EnvIntensity), env.m_intensity);

        // Not inside either gate: the cubes and the LTC tables are read through it, and
        // each can be bound without the other.
        PushSharedSampler(*values, RHI::InputName(SharedInput::IBLSampler),
            RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear,
                                      RHI::AddressMode::Clamp));

        const uint32_t areaLightLutReady = BindAreaLightLuts(*rhiCtx) ? 1u : 0u;
        PushSharedConstant(*values, RHI::InputName(SharedInput::AreaLightLutReady), areaLightLutReady);

        // Doubles on the CPU so they stay exact across a long session; the shader only ever
        // needs the low bits, so the narrowing is deliberate.
        PushSharedConstant(*values, RHI::InputName(SharedInput::FrameNumber),
            static_cast<uint32_t>(time.m_frameNumber));
        PushSharedConstant(*values, RHI::InputName(SharedInput::GameTime),
            static_cast<float>(time.m_gameTime));
        PushSharedConstant(*values, RHI::InputName(SharedInput::PrevGameTime),
            static_cast<float>(time.m_prevGameTime));
        PushSharedConstant(*values, RHI::InputName(SharedInput::DeltaTime), time.m_deltaTime);
    }

    void SceneBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_values != RHI::NullHandle)
        {
            rhiCtx.DestoryEntity(m_values);
            m_values = RHI::NullHandle;
        }
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<LightSlot, EmissiveLightSlot>();
        }
        m_lightBuffer.Shutdown(rhiCtx);
        m_shadowViewBuffer.Shutdown();
        m_viewShadowMapBuffer.Shutdown();
        m_shadowViewSlot = RHI::NullHandle;
        m_viewShadowMapSlot = RHI::NullHandle;
    }
}
