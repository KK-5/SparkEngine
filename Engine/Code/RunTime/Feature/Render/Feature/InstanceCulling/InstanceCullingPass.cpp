#include "InstanceCullingPass.h"

#include <EASTL/algorithm.h>

#include <Math/Bit.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Command/IndirectCommands.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

#include <Pass/PassContext.h>
#include <Pass/PassCapabilities.h>
#include <Pass/ComputePass.h>

#include <RenderGraph/PassScopes.h>

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Drawable/DrawMask.h>
#include <View/ViewTags.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        //! The Build of a culling pass: the list of the instances carrying drawMaskBit, in
        //! the two buffers named, once for each of `views`.
        void BuildDrawList(
            ComputePassScopes& p,
            DrawMask drawMaskBit,
            const ViewHandleList& views,
            const RHI::AttachmentId& argumentsName,
            const RHI::AttachmentId& countName)
        {
            auto& rhiContext = *RHI::RHIExecuteContext::Current();

            uint32_t slotCount = 0;
            for (auto [entity, count] : rhiContext.GetView<InstanceSlotCount>().each())
            {
                slotCount = count.m_count;
            }

            const uint32_t viewCount = static_cast<uint32_t>(views.size());

            // Every slot may be drawn in every view: each view gets room for a draw per slot,
            // and a count of its own. Room for one view and one draw at least, so the buffers
            // exist in every frame. Powers of two, so they keep their size while views and
            // instances come and go.
            const uint32_t viewCapacity    = NextPowerOfTwo(eastl::max(viewCount, 1u));
            const uint32_t maxDrawsPerView = NextPowerOfTwo(eastl::max(slotCount, 1u));
            const uint32_t maxDrawCount    = viewCapacity * maxDrawsPerView;

            // Written by the shader and read as the arguments, and the count, of a draw.
            constexpr RHI::BufferBindFlags bindFlags =
                RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::Indirect;

            RHI::BufferDescriptor argumentsDesc;
            argumentsDesc.m_byteCount = static_cast<uint64_t>(maxDrawCount) * sizeof(RHI::DrawIndexedIndirectCommand);
            argumentsDesc.m_bindFlags = bindFlags;
            p.CreateBuffer(argumentsName, argumentsDesc);

            RHI::BufferDescriptor countDesc;
            countDesc.m_byteCount = static_cast<uint64_t>(viewCapacity) * sizeof(uint32_t);
            countDesc.m_bindFlags = bindFlags;
            p.CreateBuffer(countName, countDesc);

            const RHI::BufferViewDescriptor argumentsView = RHI::BufferViewDescriptor::CreateStructured(
                0, maxDrawCount, sizeof(RHI::DrawIndexedIndirectCommand));
            const RHI::BufferViewDescriptor countView = RHI::BufferViewDescriptor::CreateStructured(
                0, viewCapacity, sizeof(uint32_t));

            const RHI::InputName argumentsInput("g_DrawArguments");
            const RHI::InputName countInput("g_DrawCount");

            auto setConstants = [&](ComputeScope& s, uint32_t isClearCount, uint32_t firstDraw, uint32_t viewOrdinal)
            {
                s.Constant(RHI::InputName("isClearCount"), isClearCount);
                s.Constant(RHI::InputName("slotCount"), slotCount);
                s.Constant(RHI::InputName("drawMaskBit"), drawMaskBit);
                s.Constant(RHI::InputName("viewCapacity"), viewCapacity);
                s.Constant(RHI::InputName("firstDraw"), firstDraw);
                s.Constant(RHI::InputName("viewOrdinal"), viewOrdinal);
            };

            // A transient buffer holds what its memory held before, and the counts are added
            // to: zeroed first, in a Scope of its own so the writes below wait for it.
            {
                auto s = p.Scope();
                s.WriteBuffer(countName).View(countView).Bind(countInput);
                setConstants(s, 1, 0, 0);
                s.Dispatch(viewCapacity);
                s.Close();
            }

            // With no view the draws are still written once, for no reader: the passes that
            // draw them read the buffers every frame, and a buffer nothing writes cannot be read.
            const uint32_t writeCount = eastl::max(viewCount, 1u);
            for (uint32_t viewOrdinal = 0; viewOrdinal < writeCount; ++viewOrdinal)
            {
                const uint32_t firstDraw = viewOrdinal * maxDrawsPerView;

                auto s = p.Scope();
                ShaderAttachment count     = s.ReadWriteBuffer(countName).View(countView).Bind(countInput);
                ShaderAttachment arguments = s.WriteBuffer(argumentsName).View(argumentsView).Bind(argumentsInput);
                if (viewOrdinal < viewCount)
                {
                    const RHI::RHIHandle view = views[viewOrdinal];
                    count.IndirectArgumentsOf(view, static_cast<uint64_t>(viewOrdinal) * sizeof(uint32_t));
                    arguments.IndirectArgumentsOf(view,
                        static_cast<uint64_t>(firstDraw) * sizeof(RHI::DrawIndexedIndirectCommand));
                }
                setConstants(s, 0, firstDraw, viewOrdinal);
                // One thread with no slot: a dispatch of no group is a debug layer warning, and
                // a thread past slotCount writes nothing.
                s.Dispatch(eastl::max(slotCount, 1u));
                s.Close();
            }
        }
    }

    const RHI::AttachmentId& InstanceCullingPass::OpaqueDrawArgumentsName()
    {
        static const RHI::AttachmentId name("OpaqueDrawArguments");
        return name;
    }

    const RHI::AttachmentId& InstanceCullingPass::ShadowCasterDrawArgumentsName()
    {
        static const RHI::AttachmentId name("ShadowCasterDrawArguments");
        return name;
    }

    const RHI::AttachmentId& InstanceCullingPass::OpaqueDrawCountName()
    {
        static const RHI::AttachmentId name("OpaqueDrawCount");
        return name;
    }

    const RHI::AttachmentId& InstanceCullingPass::ShadowCasterDrawCountName()
    {
        static const RHI::AttachmentId name("ShadowCasterDrawCount");
        return name;
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
            .Binds<InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                ViewHandleList views;
                CollectViews<MainViewTag>(*RHI::RHIExecuteContext::Current(), views);
                BuildDrawList(p, DrawMaskOf<OpaqueTag>(), views, OpaqueDrawArgumentsName(), OpaqueDrawCountName());
            })
            .Finalize();

        SPARK_COMPUTE_PASS(ctx, "ShadowCasterInstanceCulling")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                ViewHandleList views;
                CollectViews<ShadowViewTag>(*RHI::RHIExecuteContext::Current(), views);
                BuildDrawList(p, DrawMaskOf<ShadowCasterTag>(), views,
                    ShadowCasterDrawArgumentsName(), ShadowCasterDrawCountName());
            })
            .Finalize();
    }
}
