#include "InstanceCullingPass.h"

#include <EASTL/algorithm.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

#include <Pass/PassContext.h>
#include <Pass/PassCapabilities.h>
#include <Pass/ComputePass.h>

#include <RenderGraph/PassScopes.h>

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Binding/View/ViewBinding.h>
#include <Drawable/DrawMask.h>
#include <Feature/InstanceLists/InstanceLists.h>
#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        //! The Build of a culling pass: the visible instances carrying drawMaskBit, in the two
        //! buffers named, once for each of `views`.
        void BuildVisibleList(
            ComputePassScopes& p,
            DrawMask drawMaskBit,
            const ViewHandleList& views,
            const RHI::AttachmentId& instancesName,
            const RHI::AttachmentId& countName)
        {
            const InstanceLists::CullingLayout layout = InstanceLists::LayOutCulling(*RHI::RHIExecuteContext::Current(), views);
            const uint32_t viewCount  = static_cast<uint32_t>(layout.m_views.size());
            const uint32_t entryCount = layout.m_viewCapacity * layout.m_maxPerView;

            // Without the active list, the instances and their geometry there is no instance to go
            // through, and none of them is read.
            const RHI::AttachmentId idsName("InstanceIDs");
            const RHI::AttachmentId instancesPoolName("Instances");
            const RHI::AttachmentId geometriesPoolName("Geometries");
            const bool     hasInstances  = p.Import<InstanceIDBufferTag>(idsName)
                                        && p.Import<InstancePoolTag>(instancesPoolName)
                                        && p.Import<GeometryPoolTag>(geometriesPoolName);
            const uint32_t instanceCount = hasInstances ? layout.m_instanceCount : 0;

            constexpr RHI::BufferBindFlags bindFlags = RHI::BufferBindFlags::ShaderReadWrite;

            RHI::BufferDescriptor instancesDesc;
            instancesDesc.m_byteCount = static_cast<uint64_t>(entryCount) * sizeof(uint32_t);
            instancesDesc.m_bindFlags = bindFlags;
            p.CreateBuffer(instancesName, instancesDesc);

            RHI::BufferDescriptor countDesc;
            countDesc.m_byteCount = static_cast<uint64_t>(layout.m_viewCapacity) * sizeof(uint32_t);
            countDesc.m_bindFlags = bindFlags;
            p.CreateBuffer(countName, countDesc);

            const RHI::BufferViewDescriptor instancesView = RHI::BufferViewDescriptor::CreateStructured(
                0, entryCount, sizeof(uint32_t));
            const RHI::BufferViewDescriptor countView = RHI::BufferViewDescriptor::CreateStructured(
                0, layout.m_viewCapacity, sizeof(uint32_t));

            const RHI::BufferViewDescriptor idsView = RHI::BufferViewDescriptor::CreateStructured(
                0, InstanceCapacity, sizeof(uint32_t));

            const RHI::InputName instancesInput("g_VisibleInstances");
            const RHI::InputName countInput("g_VisibleCount");
            const RHI::InputName idsInput("g_InstanceIDs");

            // Every Scope sets all of them.
            struct ScopeConstants
            {
                uint32_t m_isClearCount   = 0;
                uint32_t m_instanceCount  = 0;
                uint32_t m_viewFirstEntry = 0;
                uint32_t m_viewOrdinal    = 0;
                uint32_t m_viewIndex      = 0;
            };
            auto setConstants = [&](ComputeScope& s, const ScopeConstants& constants)
            {
                s.Constant(RHI::InputName("isClearCount"), constants.m_isClearCount);
                s.Constant(RHI::InputName("instanceCount"), constants.m_instanceCount);
                s.Constant(RHI::InputName("drawMaskBit"), drawMaskBit);
                s.Constant(RHI::InputName("viewCapacity"), layout.m_viewCapacity);
                s.Constant(RHI::InputName("viewFirstEntry"), constants.m_viewFirstEntry);
                s.Constant(RHI::InputName("viewOrdinal"), constants.m_viewOrdinal);
                s.Constant(RHI::InputName("viewIndex"), constants.m_viewIndex);
            };

            // A transient buffer holds what its memory held before, and the counts are added
            // to: zeroed first, in a Scope of its own so the writes below wait for it.
            {
                ScopeConstants constants;
                constants.m_isClearCount = 1;

                auto s = p.Scope();
                s.WriteBuffer(countName).View(countView).Bind(countInput);
                setConstants(s, constants);
                s.Dispatch(layout.m_viewCapacity);
                s.Close();
            }

            // With no view the buffers are still written once, with no instance to go through:
            // the passes that read them do so every frame, and a buffer nothing writes cannot be read.
            const uint32_t writeCount = eastl::max(viewCount, 1u);
            for (uint32_t viewOrdinal = 0; viewOrdinal < writeCount; ++viewOrdinal)
            {
                ScopeConstants constants;
                constants.m_viewFirstEntry = viewOrdinal * layout.m_maxPerView;
                constants.m_viewOrdinal    = viewOrdinal;
                if (viewOrdinal < viewCount)
                {
                    constants.m_instanceCount = instanceCount;
                    constants.m_viewIndex     = layout.m_views[viewOrdinal].m_viewIndex;
                }

                auto s = p.Scope();
                if (hasInstances)
                {
                    s.ReadBuffer(idsName).View(idsView).Bind(idsInput);
                    // Through the shared bindings.
                    s.ReadBuffer(instancesPoolName);
                    s.ReadBuffer(geometriesPoolName);
                }
                s.ReadWriteBuffer(countName).View(countView).Bind(countInput);
                s.WriteBuffer(instancesName).View(instancesView).Bind(instancesInput);
                setConstants(s, constants);
                // One thread with no instance: a dispatch of no group is a debug layer warning,
                // and a thread past instanceCount writes nothing.
                s.Dispatch(eastl::max(constants.m_instanceCount, 1u));
                s.Close();
            }
        }
    }

    void InstanceCullingPass::SetUp(PassContext& ctx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/InstanceCulling/InstanceCulling.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[InstanceCullingPass] Failed to load shader InstanceCulling.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // A pass each: the Scopes of one pass share its bindings, so one pass cannot write
        // two lists into buffers of their own. Each list is written for the views of the
        // passes that draw it.
        SPARK_COMPUTE_PASS(ctx, "OpaqueInstanceCulling")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<ViewBindingTag, InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                ViewHandleList views;
                CollectViews<MainViewTag>(*RHI::RHIExecuteContext::Current(), views);
                BuildVisibleList(p, DrawMaskOf<OpaqueTag>(), views,
                    InstanceLists::Opaque::VisibleInstancesName(), InstanceLists::Opaque::VisibleCountName());
            })
            .Finalize();

        SPARK_COMPUTE_PASS(ctx, "ShadowCasterInstanceCulling")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<ViewBindingTag, InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                ViewHandleList views;
                CollectViews<ShadowViewTag>(*RHI::RHIExecuteContext::Current(), views);
                BuildVisibleList(p, DrawMaskOf<ShadowCasterTag>(), views,
                    InstanceLists::ShadowCaster::VisibleInstancesName(), InstanceLists::ShadowCaster::VisibleCountName());
            })
            .Finalize();
    }
}
