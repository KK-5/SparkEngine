#include "CopyFrameBufferPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/CopyItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Context/RHIContext.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <Pass/PassAccess.h>

#include <UI/UIBaseSystem.h>

namespace Spark::Render
{
    void CopyFrameBufferPass::SetUp(PassContext& ctx)
    {
        // Writes the swap chain back buffer, which DX12 binds to the queue the
        // swap chain was created on (Graphics). A command list touching the back
        // buffer must run on that queue, so this copy cannot use the Copy queue.
        SPARK_COPY_PASS(ctx, "CopyFrameBufferPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .Build([](RenderGraphBuilder& builder)
            {
                Render::ImageAttachmentBindInfo bind;
                bind.m_slot  = RHI::InputName("CopySource");
                bind.m_usage = RHI::AttachmentUsage::Copy;
                bind.m_stage = RHI::AttachmentStage::Copy;
                builder.ReadImageAttachment<SPARK_PASS_TAG("CopyFrameBufferPass")>(
                    RHI::AttachmentId("SceneColor"), bind);

                Render::ImportedImageAttachmentBindInfo outputBind;
                outputBind.m_slot   = RHI::InputName("CopyWrite");
                outputBind.m_usage  = RHI::AttachmentUsage::Copy;
                outputBind.m_stage  = RHI::AttachmentStage::Copy;
                outputBind.m_access = RHI::AttachmentAccess::Write;
                outputBind.m_view   = builder.GetCurrentSwapChainView();

                builder.ImportImageAttachment<SPARK_PASS_TAG("CopyFrameBufferPass")>(
                    RHI::AttachmentId("SwapChain"), outputBind);

            })
            .Execute([](ExecuteWork& work, RenderGraphExecuter& executer){
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();

                auto* copySource = FindPassAttachmentImage<SPARK_PASS_TAG("CopyFrameBufferPass")>(rhiCtx, RHI::InputName("CopySource"));
                auto* copyDst = FindPassAttachmentImage<SPARK_PASS_TAG("CopyFrameBufferPass")>(rhiCtx, RHI::InputName("CopyWrite"));
                if (!copySource || !copyDst)
                {
                    return;
                }

                RHI::Origin dstOrigin{};
                const auto* ui = Service<UI::UIBaseSystem>::Get();
                if (ui)
                {
                    Math::Vector2Int pos = ui->GetFrameBufferPos();
                    dstOrigin = RHI::Origin{static_cast<uint32_t>(pos.x), static_cast<uint32_t>(pos.y), 0};
                }

                RHI::CopyItem item;
                item.m_type = RHI::CopyItemType::Image;
                item.m_image.m_sourceImage = copySource;
                item.m_image.m_sourceOrigin = RHI::Origin();
                item.m_image.m_sourceSize = copySource->GetDescriptor().m_size;
                item.m_image.m_sourceSubresource = RHI::ImageSubresource();
                item.m_image.m_destinationImage = copyDst;
                item.m_image.m_destinationOrigin = dstOrigin;
                item.m_image.m_destinationSubresource = RHI::ImageSubresource();

                work.m_commandList->Submit(item);
            })
            .Finalize()
        ;
    }
}