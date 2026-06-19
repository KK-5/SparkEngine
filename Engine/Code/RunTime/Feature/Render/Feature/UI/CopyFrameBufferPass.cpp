#include "CopyFrameBufferPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Context/RHIContext.h>

#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <Pass/PassAccess.h>
#include <Pass/CopyPass.h>

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
                outputBind.m_action.m_loadAction = RHI::AttachmentLoadAction::Clear;
                outputBind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;
                outputBind.m_image = builder.GetCurrentSwapChainResource();

                builder.ImportImageAttachment<SPARK_PASS_TAG("CopyFrameBufferPass")>(
                    RHI::AttachmentId("SwapChain"), outputBind);

            })
            .Execute([](ExecuteWork& work, RenderGraphExecuter& executer){
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();

                // The copy itself is compiled from the CopyRequest (produced by
                // UIProcessFeature) into a CopyItem by the pass's default Compile.
                // This override only wraps that copy with the clear-to-black, so we
                // resolve just the destination (swapchain) here — for the clear view
                // and the surrounding RENDER_TARGET<->COPY_DEST barriers.
                auto* copyDst = FindPassAttachmentImage<SPARK_PASS_TAG("CopyFrameBufferPass")>(rhiCtx, RHI::InputName("CopyWrite"));
                if (!copyDst)
                {
                    return;
                }

                // ExecutePreBarriers has already transitioned the swapchain to COPY_DEST.
                // Push it to RENDER_TARGET temporarily so ClearRenderTarget can run,
                // then return it to COPY_DEST for the copy below.
                RHI::ImageBarrier toRT;
                toRT.m_image     = copyDst;
                toRT.m_srcUsage  = RHI::AttachmentUsage::Copy;
                toRT.m_dstUsage  = RHI::AttachmentUsage::RenderTarget;
                toRT.m_srcAccess = RHI::AttachmentAccess::Write;
                toRT.m_dstAccess = RHI::AttachmentAccess::Write;
                toRT.m_srcStage  = RHI::AttachmentStage::Copy;
                toRT.m_dstStage  = RHI::AttachmentStage::ColorAttachmentOutput;
                toRT.m_srcQueue  = RHI::HardwareQueueClass::Graphics;
                toRT.m_dstQueue  = RHI::HardwareQueueClass::Graphics;
                work.m_commandList->QueueBarrier(toRT);
                work.m_commandList->FlushBarriers();

                auto* swapChainView = FindPassAttachmentImageView<SPARK_PASS_TAG("CopyFrameBufferPass")>(rhiCtx, RHI::InputName("CopyWrite"), executer.GetFrameIndex());
                if (swapChainView)
                {
                    RHI::ImageClearRequest clearReq;
                    clearReq.m_clearValue = RHI::ClearValue::CreateVector4Float(0.f, 0.f, 0.f, 1.f);
                    clearReq.m_imageView = swapChainView;
                    work.m_commandList->ClearRenderTarget(clearReq);
                }

                RHI::ImageBarrier toCopy;
                toCopy.m_image     = copyDst;
                toCopy.m_srcUsage  = RHI::AttachmentUsage::RenderTarget;
                toCopy.m_dstUsage  = RHI::AttachmentUsage::Copy;
                toCopy.m_srcAccess = RHI::AttachmentAccess::Write;
                toCopy.m_dstAccess = RHI::AttachmentAccess::Write;
                toCopy.m_srcStage  = RHI::AttachmentStage::ColorAttachmentOutput;
                toCopy.m_dstStage  = RHI::AttachmentStage::Copy;
                toCopy.m_srcQueue  = RHI::HardwareQueueClass::Graphics;
                toCopy.m_dstQueue  = RHI::HardwareQueueClass::Graphics;
                work.m_commandList->QueueBarrier(toCopy);
                work.m_commandList->FlushBarriers();

                // Submit the compiled copy — its source/dest images and all scalar
                // parameters were already resolved by the default Compile.
                SubmitPassCopyItems<SPARK_PASS_TAG("CopyFrameBufferPass")>(work, executer);
            })
            .Finalize()
        ;
    }
}