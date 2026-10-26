#include "InstanceBinningPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/IndirectCommands.h>
#include <RHI/Component/Component.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

#include <Pass/PassContext.h>
#include <Pass/PassCapabilities.h>
#include <Pass/ComputePass.h>
#include <Pass/Component/PassIndirectLayout.h>

#include <RenderGraph/PassScopes.h>

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Feature/InstanceLists/InstanceLists.h>
#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        //! The pass carrying Tag, declared after the binning pass but there by the time it builds.
        template<typename Tag>
        Pass FindPass()
        {
            auto view = PassExecuteContext::Current()->GetView<Tag>();
            ASSERT(view.size() == 1, "[InstanceBinningPass] The pass it bins for is not declared.");
            return *view.begin();
        }

        //! The visible list a binning pass reads: the one InstanceCullingPass writes for the
        //! views of the pass it bins for.
        struct VisibleList
        {
            const RHI::AttachmentId& m_instancesName;
            const RHI::AttachmentId& m_countName;
        };

        //! The Build of a binning pass: drawPass's groups of the instances `views` see, in the two
        //! buffers named, its PassIndirectLayout table imported under layoutName.
        void BuildBins(
            ComputePassScopes& p,
            Pass drawPass,
            const ViewHandleList& views,
            const VisibleList& visible,
            const RHI::AttachmentId& layoutName,
            const RHI::AttachmentId& argumentsName,
            const RHI::AttachmentId& countName)
        {
            auto& rhiContext  = *RHI::RHIExecuteContext::Current();
            auto& passContext = *PassExecuteContext::Current();

            // The same layout the culling pass wrote the visible list in.
            const InstanceLists::CullingLayout culling   = InstanceLists::LayOutCulling(rhiContext, views);
            const uint32_t                     viewCount = static_cast<uint32_t>(culling.m_views.size());

            // Without the active list and the instances the culling saw no instance, and neither
            // is read.
            const RHI::AttachmentId idsName("InstanceIDs");
            const RHI::AttachmentId instancesPoolName("Instances");
            const bool hasInstances = p.Import<InstanceIDBufferTag>(idsName)
                                   && p.Import<InstancePoolTag>(instancesPoolName);

            const PassIndirectLayout& layout = passContext.Get<PassIndirectLayout>(drawPass);

            // DrawGroupSystem made it before this frame began, so it was created at the frame's start.
            ASSERT(rhiContext.Has<RHI::Components::BufferPerFrame>(layout.m_table),
                "[InstanceBinningPass] {}'s indirect layout has no table.",
                passContext.Get<PassName>(drawPass).m_name.GetCStr());
            p.Import(layoutName, layout.m_table);

            const uint32_t recordCount = culling.m_viewCapacity * layout.m_recordsPerView;
            const uint32_t countCount  = culling.m_viewCapacity * layout.m_countsPerView;

            // Written by the shader and read as the arguments, and the counts, of draws.
            constexpr RHI::BufferBindFlags bindFlags =
                RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::Indirect;

            RHI::BufferDescriptor argumentsDesc;
            argumentsDesc.m_byteCount = static_cast<uint64_t>(recordCount) * sizeof(RHI::DrawIndexedIndirectCommand);
            argumentsDesc.m_bindFlags = bindFlags;
            p.CreateBuffer(argumentsName, argumentsDesc);

            RHI::BufferDescriptor countDesc;
            countDesc.m_byteCount = static_cast<uint64_t>(countCount) * sizeof(uint32_t);
            countDesc.m_bindFlags = bindFlags;
            p.CreateBuffer(countName, countDesc);

            const RHI::BufferViewDescriptor visibleInstancesView = RHI::BufferViewDescriptor::CreateStructured(
                0, culling.m_viewCapacity * culling.m_maxPerView, sizeof(uint32_t));
            const RHI::BufferViewDescriptor visibleCountView = RHI::BufferViewDescriptor::CreateStructured(
                0, culling.m_viewCapacity, sizeof(uint32_t));
            const RHI::BufferViewDescriptor layoutView = RHI::BufferViewDescriptor::CreateStructured(
                0, layout.m_tableLength, sizeof(IndirectLayoutEntry));
            const RHI::BufferViewDescriptor argumentsView = RHI::BufferViewDescriptor::CreateStructured(
                0, recordCount, sizeof(RHI::DrawIndexedIndirectCommand));
            const RHI::BufferViewDescriptor countView = RHI::BufferViewDescriptor::CreateStructured(
                0, countCount, sizeof(uint32_t));
            const RHI::BufferViewDescriptor idsView = RHI::BufferViewDescriptor::CreateStructured(
                0, InstanceCapacity, sizeof(uint32_t));

            const RHI::InputName idsInput("g_InstanceIDs");
            const RHI::InputName visibleInstancesInput("g_VisibleInstances");
            const RHI::InputName visibleCountInput("g_VisibleCount");
            const RHI::InputName layoutInput("g_IndirectLayout");
            const RHI::InputName argumentsInput("g_DrawArguments");
            const RHI::InputName countInput("g_DrawCount");

            // Every Scope sets all of them.
            struct ScopeConstants
            {
                uint32_t m_isClearCount    = 0;
                uint32_t m_entryCount      = 0;
                uint32_t m_viewFirstEntry  = 0;
                uint32_t m_viewOrdinal     = 0;
                uint32_t m_viewFirstCount  = 0;
                uint32_t m_viewFirstRecord = 0;
            };
            auto setConstants = [&](ComputeScope& s, const ScopeConstants& constants)
            {
                s.Constant(RHI::InputName("isClearCount"), constants.m_isClearCount);
                s.Constant(RHI::InputName("countCapacity"), countCount);
                s.Constant(RHI::InputName("indirectLayoutLength"), layout.m_tableLength);
                s.Constant(RHI::InputName("entryCount"), constants.m_entryCount);
                s.Constant(RHI::InputName("viewFirstEntry"), constants.m_viewFirstEntry);
                s.Constant(RHI::InputName("viewOrdinal"), constants.m_viewOrdinal);
                s.Constant(RHI::InputName("viewFirstCount"), constants.m_viewFirstCount);
                s.Constant(RHI::InputName("viewFirstRecord"), constants.m_viewFirstRecord);
            };

            // A transient buffer holds what its memory held before, and the counts are added
            // to: zeroed first, in a Scope of its own so the writes below wait for it.
            {
                ScopeConstants constants;
                constants.m_isClearCount = 1;

                auto s = p.Scope();
                s.WriteBuffer(countName).View(countView).Bind(countInput);
                setConstants(s, constants);
                s.Dispatch(countCount);
                s.Close();
            }

            // With no view the buffers are still written once, with no entry to go through: the
            // pass that draws them reads the buffers every frame, and a buffer nothing writes
            // cannot be read.
            const uint32_t writeCount = eastl::max(viewCount, 1u);
            for (uint32_t viewOrdinal = 0; viewOrdinal < writeCount; ++viewOrdinal)
            {
                ScopeConstants constants;
                constants.m_viewFirstEntry  = viewOrdinal * culling.m_maxPerView;
                constants.m_viewOrdinal     = viewOrdinal;
                constants.m_viewFirstCount  = viewOrdinal * layout.m_countsPerView;
                constants.m_viewFirstRecord = viewOrdinal * layout.m_recordsPerView;

                auto s = p.Scope();
                if (hasInstances)
                {
                    s.ReadBuffer(idsName).View(idsView).Bind(idsInput);
                    // Through the shared binding.
                    s.ReadBuffer(instancesPoolName);
                }
                s.ReadBuffer(visible.m_instancesName).View(visibleInstancesView).Bind(visibleInstancesInput);
                s.ReadBuffer(visible.m_countName).View(visibleCountView).Bind(visibleCountInput);
                s.ReadBuffer(layoutName).View(layoutView).Bind(layoutInput);
                ShaderAttachment count     = s.ReadWriteBuffer(countName).View(countView).Bind(countInput);
                ShaderAttachment arguments = s.WriteBuffer(argumentsName).View(argumentsView).Bind(argumentsInput);
                if (viewOrdinal < viewCount)
                {
                    const RHI::RHIHandle view = culling.m_views[viewOrdinal].m_view;
                    count.IndirectArgumentsOf(view, static_cast<uint64_t>(constants.m_viewFirstCount) * sizeof(uint32_t));
                    arguments.IndirectArgumentsOf(view,
                        static_cast<uint64_t>(constants.m_viewFirstRecord) * sizeof(RHI::DrawIndexedIndirectCommand));
                    // A view sees at most every instance.
                    constants.m_entryCount = hasInstances ? culling.m_instanceCount : 0;
                }
                setConstants(s, constants);
                // One thread with no entry: a dispatch of no group is a debug layer warning, and
                // a thread past entryCount writes nothing.
                s.Dispatch(eastl::max(constants.m_entryCount, 1u));
                s.Close();
            }
        }
    }

    void InstanceBinningPass::SetUp(PassContext& ctx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "AssetManager is unregister.");

        Resource::AssetId assetId = assetManager->MakeAssetId("engine://Shaders/InstanceBinning/InstanceBinning.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[InstanceBinningPass] Failed to load shader InstanceBinning.hlsl.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);

        // A pass each, as the culling passes are: the Scopes of one pass share its bindings.
        SPARK_COMPUTE_PASS(ctx, "DepthPrePassInstanceBinning")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                ViewHandleList views;
                CollectViews<MainViewTag>(*RHI::RHIExecuteContext::Current(), views);
                BuildBins(p, FindPass<SPARK_PASS_TAG("DepthPrePass")>(), views,
                    VisibleList{ InstanceLists::Opaque::VisibleInstancesName(), InstanceLists::Opaque::VisibleCountName() },
                    InstanceLists::DepthPre::IndirectLayoutName(),
                    InstanceLists::DepthPre::DrawArgumentsName(), InstanceLists::DepthPre::DrawCountName());
            })
            .Finalize();

        SPARK_COMPUTE_PASS(ctx, "GBufferPassInstanceBinning")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                ViewHandleList views;
                CollectViews<MainViewTag>(*RHI::RHIExecuteContext::Current(), views);
                BuildBins(p, FindPass<SPARK_PASS_TAG("GBufferPass")>(), views,
                    VisibleList{ InstanceLists::Opaque::VisibleInstancesName(), InstanceLists::Opaque::VisibleCountName() },
                    InstanceLists::GBuffer::IndirectLayoutName(),
                    InstanceLists::GBuffer::DrawArgumentsName(), InstanceLists::GBuffer::DrawCountName());
            })
            .Finalize();

        SPARK_COMPUTE_PASS(ctx, "ShadowPassInstanceBinning")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                ViewHandleList views;
                CollectViews<ShadowViewTag>(*RHI::RHIExecuteContext::Current(), views);
                BuildBins(p, FindPass<SPARK_PASS_TAG("ShadowPass")>(), views,
                    VisibleList{ InstanceLists::ShadowCaster::VisibleInstancesName(),
                                 InstanceLists::ShadowCaster::VisibleCountName() },
                    InstanceLists::Shadow::IndirectLayoutName(),
                    InstanceLists::Shadow::DrawArgumentsName(), InstanceLists::Shadow::DrawCountName());
            })
            .Finalize();
    }
}
