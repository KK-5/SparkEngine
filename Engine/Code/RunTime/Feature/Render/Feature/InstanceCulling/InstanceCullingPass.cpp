#include "InstanceCullingPass.h"

#include <EASTL/algorithm.h>

#include <Math/Bit.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Command/IndirectCommands.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>

#include <Pass/PassContext.h>
#include <Pass/ComputePass.h>

#include <RenderGraph/PassScopes.h>

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Drawable/DrawMask.h>

#include <Resource/AssetManagerInterface.h>

namespace Spark::Render
{
    namespace
    {
        //! The Build of a culling pass: the list of the instances carrying drawMaskBit, in
        //! the two buffers named.
        void BuildDrawList(
            ComputePassScopes& p,
            DrawMask drawMaskBit,
            const RHI::AttachmentId& argumentsName,
            const RHI::AttachmentId& countName)
        {
            auto& rhiContext = *RHI::RHIExecuteContext::Current();

            uint32_t slotCount = 0;
            for (auto [entity, count] : rhiContext.GetView<InstanceSlotCount>().each())
            {
                slotCount = count.m_count;
            }

            // Every slot may be drawn. A power of two, so the buffer keeps its size while
            // instances come and go; one record with no slot at all, so the list exists in
            // every frame and holds a count of 0.
            const uint32_t capacity = NextPowerOfTwo(eastl::max(slotCount, 1u));

            // Written by the shader and read as the arguments, and the count, of a draw.
            constexpr RHI::BufferBindFlags bindFlags =
                RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::Indirect;

            RHI::BufferDescriptor argumentsDesc;
            argumentsDesc.m_byteCount = static_cast<uint64_t>(capacity) * sizeof(RHI::DrawIndexedIndirectCommand);
            argumentsDesc.m_bindFlags = bindFlags;
            p.CreateBuffer(argumentsName, argumentsDesc);

            RHI::BufferDescriptor countDesc;
            countDesc.m_byteCount = sizeof(uint32_t);
            countDesc.m_bindFlags = bindFlags;
            p.CreateBuffer(countName, countDesc);

            const RHI::BufferViewDescriptor argumentsView = RHI::BufferViewDescriptor::CreateStructured(
                0, capacity, sizeof(RHI::DrawIndexedIndirectCommand));
            const RHI::BufferViewDescriptor countView = RHI::BufferViewDescriptor::CreateStructured(
                0, 1, sizeof(uint32_t));

            const RHI::InputName argumentsInput("g_DrawArguments");
            const RHI::InputName countInput("g_DrawCount");

            auto setConstants = [&](ComputeScope& s, uint32_t isClearCount)
            {
                s.Constant(RHI::InputName("isClearCount"), isClearCount);
                s.Constant(RHI::InputName("slotCount"), slotCount);
                s.Constant(RHI::InputName("drawMaskBit"), drawMaskBit);
            };

            // A transient buffer holds what its memory held before, and the count is added to:
            // zeroed first, in a Scope of its own so the writes below wait for it.
            {
                auto s = p.Scope();
                s.WriteBuffer(countName).View(countView).Bind(countInput);
                setConstants(s, 1);
                s.Dispatch(1);
                s.Close();
            }

            {
                auto s = p.Scope();
                s.ReadWriteBuffer(countName).View(countView).Bind(countInput);
                s.WriteBuffer(argumentsName).View(argumentsView).Bind(argumentsInput);
                setConstants(s, 0);
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
        // two lists into buffers of their own.
        SPARK_COMPUTE_PASS(ctx, "OpaqueInstanceCulling")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                BuildDrawList(p, DrawMaskOf<OpaqueTag>(), OpaqueDrawArgumentsName(), OpaqueDrawCountName());
            })
            .Finalize();

        SPARK_COMPUTE_PASS(ctx, "ShadowCasterInstanceCulling")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(shaderAsset)
            .Binds<InstanceBindingTag, GeometryBindingTag>()
            .Build([](ComputePassScopes& p)
            {
                BuildDrawList(p, DrawMaskOf<ShadowCasterTag>(),
                    ShadowCasterDrawArgumentsName(), ShadowCasterDrawCountName());
            })
            .Finalize();
    }
}
