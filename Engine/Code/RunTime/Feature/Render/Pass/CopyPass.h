#pragma once

#include <Log/ILogSystem.h>
#include <EASTL/functional.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Command/CopyItem.h>
#include <RHI/Command/CommandList.h>
#include <RHI/HardwareQueue.h>

#include <Pass/PassAccess.h>
#include <Pass/PassTag.h>
#include <Pass/Component/PassComponents.h>
#include <Request/CopyRequest.h>
#include <RenderGraph/RenderGraphExecuter.h>

namespace Spark::Render
{
    //! Default Compile body for a copy pass. Resolves every CopyRequest tagged
    //! with PassTag into a submittable RHI::CopyItem stored back on the same
    //! entity. slot → resource lookup is fully PassTag-scoped (the request never
    //! names a resolved resource), so this runs per-pass where PassTag is known.
    //!
    //! Patches the resource pointers the Processor left null according to the
    //! request's CopyItemType; for image sources a zero Size is filled with the
    //! resolved image's full descriptor size. Re-run every frame so swap-chain /
    //! transient backing rotation is picked up.
    template<typename PassTag>
    void CompilePassCopyRequests(RenderGraphCompiler&)
    {
        auto& ctx = *RHI::RHIExecuteContext::Current();
        ctx.GetView<PassTag, CopyRequest>().each([&](RHIHandle handle, const CopyRequest& req)
        {
            // Only the resources are resolved per-pass (by slot name). Every scalar
            // parameter — including source size — is already settled in the request
            // by the Processor; the pass must not reverse-derive anything from the
            // resolved resource.
            RHI::CopyItem item = req.m_template;
            switch (item.m_type)
            {
            case RHI::CopyItemType::Buffer:
            {
                item.m_buffer.m_sourceBuffer      = FindPassAttachmentBuffer<PassTag>(ctx, req.m_sourceSlot);
                item.m_buffer.m_destinationBuffer = FindPassAttachmentBuffer<PassTag>(ctx, req.m_destSlot);
                break;
            }
            case RHI::CopyItemType::Image:
            {
                item.m_image.m_sourceImage      = FindPassAttachmentImage<PassTag>(ctx, req.m_sourceSlot);
                item.m_image.m_destinationImage = FindPassAttachmentImage<PassTag>(ctx, req.m_destSlot);
                break;
            }
            case RHI::CopyItemType::BufferToImage:
            {
                item.m_bufferToImage.m_sourceBuffer     = FindPassAttachmentBuffer<PassTag>(ctx, req.m_sourceSlot);
                item.m_bufferToImage.m_destinationImage = FindPassAttachmentImage<PassTag>(ctx, req.m_destSlot);
                break;
            }
            case RHI::CopyItemType::ImageToBuffer:
            {
                item.m_imageToBuffer.m_sourceImage       = FindPassAttachmentImage<PassTag>(ctx, req.m_sourceSlot);
                item.m_imageToBuffer.m_destinationBuffer = FindPassAttachmentBuffer<PassTag>(ctx, req.m_destSlot);
                break;
            }
            default:
            {
                LOG_ERROR("[CompilePassCopyRequests] Unsupported CopyItemType {}.",
                    static_cast<uint32_t>(item.m_type));
                break;
            }
            }
            ctx.AddOrReplace<RHI::CopyItem>(handle, item);
        });
    }

    //! Default Execute body for a copy pass. Submits every compiled CopyItem
    //! tagged with PassTag to the pass's command list. Pure submit, no external
    //! dependency — the Processor owns all upper-layer data collection.
    template<typename PassTag>
    void SubmitPassCopyItems(ExecuteWork& work, RenderGraphExecuter&)
    {
        auto& ctx = *RHI::RHIExecuteContext::Current();
        ctx.GetView<PassTag, RHI::CopyItem>().each([&](RHIHandle, const RHI::CopyItem& item)
        {
            work.m_commandList->Submit(item);
        });
    }

    // ================================================================
    // CopyPassBuilder<PassTag>
    //
    // For passes that issue raw command-list copy/blit/resolve work: no
    // shaders, no PSO, no render targets. Always a custom-pipeline pass and
    // defaults to the Copy queue (override via Queue() if the copy must run on
    // Graphics/Compute). Build declares the source/dest attachment slots; if
    // Compile/Execute are not overridden, Finalize installs the defaults
    // (CompilePassCopyRequests / SubmitPassCopyItems) that resolve those slots
    // into a CopyItem and submit it. Lives here (not in the common PassBuilder.h)
    // so its dependency on PassAccess.h only taxes the files that write copy passes.
    // ================================================================
    template<typename PassTag>
    class CopyPassBuilder
    {
    public:
        using BuildFunction   = eastl::function<void(RenderGraphBuilder&)>;
        using CompileFunction = eastl::function<void(RenderGraphCompiler&)>;
        using ExecuteFunction = eastl::function<void(ExecuteWork&, RenderGraphExecuter&)>;

        CopyPassBuilder& Queue(RHI::HardwareQueueClass q)
        {
            m_queue = q;
            return *this;
        }

        CopyPassBuilder& Inactive()
        {
            m_active = false;
            return *this;
        }

        // ---- Functions ----
        CopyPassBuilder& Build(BuildFunction fn)
        {
            m_buildFunction = eastl::move(fn);
            return *this;
        }

        CopyPassBuilder& Compile(CompileFunction fn)
        {
            m_compileFunction = eastl::move(fn);
            return *this;
        }

        CopyPassBuilder& Execute(ExecuteFunction fn)
        {
            m_executeFunction = eastl::move(fn);
            return *this;
        }

        // ---- Finalize ----
        Pass Finalize()
        {
            ASSERT(!m_finalized, "Copy pass '{}' is already finalized.", m_name.GetCStr());
            ASSERT(m_buildFunction, "Copy pass '{}': Build function is required.", m_name.GetCStr());

            Pass pass = m_context->CreatePass();

            ASSERT(m_context->GetView<PassTag>().size() == 0,
                "Copy pass '{}' tag collides with an existing pass (duplicate name or 32-bit hash collision).",
                m_name.GetCStr());
            m_context->Add<PassTag>(pass);

            m_context->Add<PassName>(pass, PassName{m_name});
            m_context->Add<CopyPassTag>(pass);
            // A copy pass never owns an engine PSO; tag it so the PSO compiler skips it.
            m_context->Add<CustomPipelinePassTag>(pass);
            m_context->Add<PassExecuteQueue>(pass, PassExecuteQueue{m_queue});
            m_context->Add<PassAttachmentMarker>(pass, MarkPassAttachmentCompiling<PassTag>());

            if (m_active)
                m_context->Add<ActivePassTag>(pass);

            // Install the slot-resolving defaults unless the caller overrode them.
            PassFunctions funcs;
            funcs.m_buildFunction   = eastl::move(m_buildFunction);
            funcs.m_compileFunction = m_compileFunction
                ? eastl::move(m_compileFunction)
                : CompileFunction(CompilePassCopyRequests<PassTag>);
            funcs.m_executeFunction = m_executeFunction
                ? eastl::move(m_executeFunction)
                : ExecuteFunction(SubmitPassCopyItems<PassTag>);
            m_context->Add<PassFunctions>(pass, eastl::move(funcs));

            m_finalized = true;
            return pass;
        }

    private:
        template<typename T>
        friend CopyPassBuilder<T> RegisterCopyPass(PassContext&, ObjectName);

        CopyPassBuilder(PassContext& ctx, ObjectName name)
            : m_context(&ctx)
            , m_name(name)
        {
        }

        PassContext*            m_context;
        ObjectName              m_name;
        RHI::HardwareQueueClass m_queue   {RHI::HardwareQueueClass::Copy};
        bool                    m_active  {true};

        BuildFunction           m_buildFunction;
        CompileFunction         m_compileFunction;
        ExecuteFunction         m_executeFunction;

        bool                    m_finalized {false};
    };

    template<typename PassTag>
    CopyPassBuilder<PassTag> RegisterCopyPass(PassContext& ctx, ObjectName name)
    {
        return CopyPassBuilder<PassTag>(ctx, name);
    }
}

#define SPARK_COPY_PASS(ctx, NAME) \
    ::Spark::Render::RegisterCopyPass<SPARK_PASS_TAG(NAME)>((ctx), ::Spark::ObjectName(NAME))
