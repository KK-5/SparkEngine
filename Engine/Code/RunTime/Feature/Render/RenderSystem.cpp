#include "RenderSystem.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <Log/SpdLogSystem.h>
#include <EASTL/any.h>
#include <EASTL/algorithm.h>

#include <RHI/SwapChain/SwapChainDescriptor.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Attachment/RenderAttachmentLayout.h>
#include <RHI/Attachment/RenderAttachmentLayoutBuilder.h>
#include <RHI/Pipeline/RenderTargetLayout.h>
#include <RHI/Pipeline/PipelineLibrary.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/PipelineStateDescriptor.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/RenderPassBeginInfo.h>
#include <RHI/Resource/Image/ImageView.h>

#include <Pass/Pass.h>
#include <Pass/PassTag.h>
#include <Pass/PassContext.h>
#include <Pass/RHIContext.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>

#include "../Window/IWindowSystem.h"
#include "../UI/UIBaseSystem.h"

#include <thread>

namespace Spark::Render
{
    template <typename AttachmentT>
    RHI::ResourceState CompileResourceState(const AttachmentT& attachment)
    {
        ASSERT(attachment.m_usage != RHI::AttachmentUsage::Uninitialized,
               "[RenderSystem] Attachment has uninitialized usage.");
        ASSERT(attachment.m_access != RHI::AttachmentAccess::Unknown,
               "[RenderSystem] Attachment has unknown access.");

        RHI::ResourceState state;
        state.m_usage  = attachment.m_usage;
        state.m_access = RHI::AdjustAccessBasedOnUsage(attachment.m_access, attachment.m_usage);
        return state;
    }

    RHI::ResourceState GetInitialState(RHIHandle resource, const RHIContext& context)
    {
        if (context.Has<ImportedTag>(resource))
        {
            ASSERT(context.Has<ImportedResourceState>(resource), "Imported resource must have ImportedResourceState.");
            return context.Get<ImportedResourceState>(resource).m_initial;
        }
        else if (context.Has<TransientTag>(resource))
        {
            return RHI::ResourceState{RHI::AttachmentUsage::Uninitialized, RHI::AttachmentAccess::Unknown};
        }

        LOG_ERROR("Resource {} has not ImportedTag nor TransientTag", context.Get<ResourceName>(resource).m_name.GetCStr());
        return RHI::ResourceState{RHI::AttachmentUsage::Uninitialized, RHI::AttachmentAccess::Unknown};
    }

    // View handle → underlying Resource handle. Falls back to the input if it is already a resource.
    RHIHandle ResolveResource(RHIHandle handle, const RHIContext& context)
    {
        if (context.Has<ViewHierarchy>(handle))
            return context.Get<ViewHierarchy>(handle).m_resource;
        return handle;
    }

    RHI::Image* GetImageForResource(RHIHandle resource, const RHIContext& context, uint32_t frameIndex)
    {
        if (context.Has<SwapChainImages>(resource))
            return context.Get<SwapChainImages>(resource).images[frameIndex];
        // TODO: support transient/persistent ImagePtr component once that is defined.
        return nullptr;
    }

    RHI::Buffer* GetBufferForResource(RHIHandle resource, const RHIContext& context)
    {
        // TODO: support transient/persistent BufferPtr component once that is defined.
        (void)resource; (void)context;
        return nullptr;
    }

    // View handle → underlying RHI::ImageView*. Mirrors GetImageForResource for views.
    RHI::ImageView* GetImageViewForView(RHIHandle viewHandle, const RHIContext& context, uint32_t frameIndex)
    {
        if (context.Has<SwapChainViews>(viewHandle))
            return context.Get<SwapChainViews>(viewHandle).imageViews[frameIndex].get();
        // TODO: support transient/persistent ImageView component once that is defined.
        return nullptr;
    }

    // Translate this pass's ImagePassAttachments (marked with AttachmentCompilingTag)
    // into a RHI::RenderPassBeginInfo component on the pass entity. Only attachments
    // with usage RenderTarget or DepthStencil contribute — non-render usages (Shader,
    // Resolve, …) leave the pass without a RenderPassBeginInfo and the execute loop
    // skips Begin/EndRenderPass for it.
    void CompileRenderPassBeginInfo(Pass pass, RHIContext& rhiContext, uint32_t frameIndex, PassContext& passContext)
    {
        RHI::RenderPassBeginInfo info;
        bool hasAny = false;

        auto view = rhiContext.GetView<ImagePassAttachment, AttachmentCompilingTag>();
        view.each([&](auto, const ImagePassAttachment& att)
        {
            RHI::ImageView* imageView = GetImageViewForView(att.m_view, rhiContext, frameIndex);
            if (!imageView)
                return;

            if (att.m_usage == RHI::AttachmentUsage::RenderTarget)
            {
                ASSERT(info.m_colorAttachmentCount < RHI::Limits::Pipeline::AttachmentColorCountMax,
                       "[RenderSystem] Too many color attachments on pass.");
                auto& color = info.m_colorAttachments[info.m_colorAttachmentCount++];
                color.m_view = imageView;
                color.m_loadStoreAction = att.m_action;
                hasAny = true;
            }
            else if (att.m_usage == RHI::AttachmentUsage::DepthStencil)
            {
                info.m_depthStencilAttachment.m_view = imageView;
                info.m_depthStencilAttachment.m_access = att.m_access;
                info.m_depthStencilAttachment.m_loadStoreAction = att.m_action;
                hasAny = true;
            }
        });

        if (hasAny)
            passContext.Add<RHI::RenderPassBeginInfo>(pass, eastl::move(info));
    }

    void CompileImageBarriers(Pass pass, RHIContext& context, uint32_t frameIndex, PassBarriers& out)
    {
        auto view = context.GetView<ImagePassAttachment, AttachmentCompilingTag>();
        view.each([&](auto, const ImagePassAttachment& att)
        {
            RHIHandle resource = ResolveResource(att.m_view, context);
            ASSERT(resource != NullHandle, "Image attachment view has no resource.");

            // Lazy-seed the per-frame state cursor on the resource entity.
            if (!context.Has<ResourceStateTracker>(resource))
            {
                ResourceStateTracker init;
                init.m_current = GetInitialState(resource, context);
                context.Add<ResourceStateTracker>(resource, init);
            }
            auto& tracker = context.Get<ResourceStateTracker>(resource);

            RHI::ResourceState src = tracker.m_current;
            RHI::ResourceState dst = CompileResourceState(att);

            // loadOp=Clear discards prior contents — let backend pick UNDEFINED for src layout.
            if (att.m_action.m_loadAction == RHI::AttachmentLoadAction::Clear)
            {
                src.m_usage  = RHI::AttachmentUsage::Uninitialized;
                src.m_access = RHI::AttachmentAccess::Unknown;
            }

            if (src != dst)
            {
                RHI::Image* image = GetImageForResource(resource, context, frameIndex);
                ASSERT(image != nullptr, "Resource has no associated RHI::Image*.");

                RHI::ImageBarrier b;
                b.m_image     = image;
                b.m_srcUsage  = src.m_usage;
                b.m_dstUsage  = dst.m_usage;
                b.m_srcAccess = src.m_access;
                b.m_dstAccess = dst.m_access;
                b.m_srcStage  = tracker.m_lastStage;
                b.m_dstStage  = att.m_stage;
                out.m_preImage.push_back(b);
            }

            tracker.m_current   = dst;
            tracker.m_lastPass  = pass;
            tracker.m_lastStage = att.m_stage;
        });
    }

    void CompileBufferBarriers(Pass pass, RHIContext& context, PassBarriers& out)
    {
        auto view = context.GetView<BufferPassAttachment, AttachmentCompilingTag>();
        view.each([&](auto, const BufferPassAttachment& att)
        {
            RHIHandle resource = ResolveResource(att.m_view, context);
            ASSERT(resource != NullHandle, "Buffer attachment view has no resource.");

            if (!context.Has<ResourceStateTracker>(resource))
            {
                ResourceStateTracker init;
                init.m_current = GetInitialState(resource, context);
                context.Add<ResourceStateTracker>(resource, init);
            }
            auto& tracker = context.Get<ResourceStateTracker>(resource);

            RHI::ResourceState src = tracker.m_current;
            RHI::ResourceState dst = CompileResourceState(att);

            if (src != dst)
            {
                RHI::Buffer* buffer = GetBufferForResource(resource, context);
                ASSERT(buffer != nullptr, "Resource has no associated RHI::Buffer*.");

                RHI::BufferBarrier b;
                b.m_buffer    = buffer;
                b.m_srcUsage  = src.m_usage;
                b.m_dstUsage  = dst.m_usage;
                b.m_srcAccess = src.m_access;
                b.m_dstAccess = dst.m_access;
                b.m_srcStage  = tracker.m_lastStage;
                b.m_dstStage  = att.m_stage;
                out.m_preBuffer.push_back(b);
            }

            tracker.m_current   = dst;
            tracker.m_lastPass  = pass;
            tracker.m_lastStage = att.m_stage;
        });
    }


    bool RenderSystem::InitRHIData()
    {
        auto rhi = Service<RHI::RHIInterface>::Get();
        if (!rhi)
        {
            LOG_ERROR("[RenderSystem] There is no RHI service.");
            return false;
        }

        m_rhiData.m_factory = rhi->GetRHIFactory();
        if (!m_rhiData.m_factory)
        {
            LOG_ERROR("[RenderSystem] Get RHI factory failed.");
            return false;
        }

        RHI::PhysicalDeviceList devList = m_rhiData.m_factory->EnumeratePhysicalDevices();
        ASSERT(devList.size() > 0, "[RenderSystem] No physical devices available.");
        Ptr<RHI::PhysicalDevice> selectDevice;
        for (Ptr<RHI::PhysicalDevice> physicalDev: devList)
        {
            // Now chose the first device
            selectDevice = physicalDev;
            break;
        }

        RHI::DeviceDescriptor deviceDesc;
        deviceDesc.m_frameCountMax = 3;
        m_rhiData.m_device = m_rhiData.m_factory->CreateDevice();
        if (m_rhiData.m_device->Init(*selectDevice, deviceDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderSystem] Init device failed.");
            return false;
        }

        m_rhiData.m_commandQueuecontext.Init(*m_rhiData.m_device);

        auto window = Service<Window::IWindowSystem>::Get();
        if (!window)
        {
            LOG_ERROR("[RenderSystem] There is no window service.");
            return false;
        }

        m_rhiData.m_swapChain = m_rhiData.m_factory->CreateSwapChain();
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount = deviceDesc.m_frameCountMax;
        desc.m_dimensions.m_imageFormat = RHI::Format::R8G8B8A8_UNORM;
        auto windowSize = window->GetWindowSize();
        desc.m_dimensions.m_imageHeight = windowSize.second;
        desc.m_dimensions.m_imageWidth = windowSize.first;
        desc.m_window = window->GetNativeHandle();
        RHI::ResultCode result = m_rhiData.m_swapChain->Init(
            *m_rhiData.m_device, 
            m_rhiData.m_commandQueuecontext.GetCommandQueue(RHI::HardwareQueueClass::Graphics),
            desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderSystem] Create swap chain failed!");
            return false;
        }

        const uint32_t imageCount = m_rhiData.m_swapChain->GetDescriptor().m_dimensions.m_imageCount;

        // Resource entity: owns the swap chain images and the synchronization state.
        // Barriers target this entity.
        m_rhiData.m_swapchainResourceHandle = m_rhiContext.CreateEntity();
        SwapChainImages swapChainImages;
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            swapChainImages.images[i] = m_rhiData.m_swapChain->GetImage(i);
        }
        m_rhiContext.Add<SwapChainImages>(m_rhiData.m_swapchainResourceHandle, eastl::move(swapChainImages));
        m_rhiContext.Add<ImportedTag>(m_rhiData.m_swapchainResourceHandle);
        m_rhiContext.Add<ResourceName>(m_rhiData.m_swapchainResourceHandle, ObjectName{"SwapChain"});
        m_rhiContext.Add<ImportedResourceState>(
            m_rhiData.m_swapchainResourceHandle,
            RHI::ResourceState{RHI::AttachmentUsage::Uninitialized, RHI::AttachmentAccess::Unknown},
            RHI::ResourceState{RHI::AttachmentUsage::Present, RHI::AttachmentAccess::Read}
        );

        // View entity: owns the ImageViews, shader bindings reference this.
        // Compile-time barrier code walks ViewHierarchy.m_resource to reach the resource entity.
        m_rhiData.m_swapchainViewHandle = m_rhiContext.CreateEntity();
        SwapChainViews swapChainView;
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            auto image = m_rhiData.m_swapChain->GetImage(i);
            Ptr<RHI::ImageView> imageview = m_rhiData.m_factory->CreateImageView();
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin = 0;
            viewDesc.m_mipSliceMax = 0;
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = 0;
            RHI::ResultCode viewResult = imageview->Init(*image, viewDesc);
            if (viewResult != RHI::ResultCode::Success)
            {
                LOG_ERROR("Create swap chain view failed.");
                continue;
            }
            swapChainView.imageViews[i] = eastl::move(imageview);
        }
        m_rhiContext.Add<SwapChainViews>(m_rhiData.m_swapchainViewHandle, eastl::move(swapChainView));
        m_rhiContext.Add<ResourceName>(m_rhiData.m_swapchainViewHandle, ObjectName{"SwapChainView"});
        m_rhiContext.Add<ViewHierarchy>(
            m_rhiData.m_swapchainViewHandle,
            m_rhiData.m_swapchainResourceHandle,
            NullHandle,
            NullHandle
        );

        m_rhiContext.Add<ResourceHierarchy>(
            m_rhiData.m_swapchainResourceHandle,
            m_rhiData.m_swapchainViewHandle
        );

        m_rhiData.m_pipelineLibrary = m_rhiData.m_factory->CreatePipelineLibrary();
        RHI::PipelineLibraryDescriptor pipelineLibraryDesc;

        result = m_rhiData.m_pipelineLibrary->Init(*m_rhiData.m_device, pipelineLibraryDesc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create pipeline library failed!");
            return false;
        }

        return true;
    }

    bool RenderSystem::InitRenderUI()
    {
        RHI::ImGuiDescriptor desc;
        desc.m_rtvFormat = RHI::Format::R8G8B8A8_UNORM;
        desc.m_dsvFormat = RHI::Format::D32_FLOAT;
        m_rednerUI.Bind(*m_rhiData.m_device, m_rhiData.m_commandQueuecontext.GetCommandQueue(RHI::HardwareQueueClass::Graphics), desc);
        return true;
    }

    void RenderSystem::BuildPipeline()
    {
        auto& passContext = m_pipeline.GetPassContext();

        Pass uiPass = passContext.CreateEntity();
        passContext.Add<PassName>(uiPass, ObjectName("UIPass"));
        passContext.Add<ActivePassTag>(uiPass);
        passContext.Add<RenderPassTag>(uiPass);
        passContext.Add<PassAttachmentMarker>(uiPass, MarkPassAttachmentCompiling<SPARK_PASS_TAG("UIPass")>());

        passContext.Add<RHI::InputStreamLayout>(uiPass);

        RHI::RenderTargetLayout renderTargetLayout;
        renderTargetLayout.m_colorAttachmentCount = 1;
        renderTargetLayout.m_colorFormats = {RHI::Format::R8G8B8A8_UNORM};
        passContext.Add<RHI::RenderTargetLayout>(uiPass, renderTargetLayout);

        passContext.Add<RHI::RenderStates>(uiPass);

        passContext.Add<PassShaders>(uiPass);

        passContext.Add<PassShaderInputs>(uiPass);

        PassFunctions uiPassFunc;
        uiPassFunc.m_buildFunction = [&](RenderGraphBuilder& builder)
        {
            ImportedImageAttachmentBindInfo bind;
            bind.m_slot   = RHI::InputName("ColorOutput");
            bind.m_view   = m_rhiData.m_swapchainViewHandle;
            bind.m_access = RHI::AttachmentAccess::Write;
            bind.m_usage  = RHI::AttachmentUsage::RenderTarget;
            bind.m_action.m_clearValue  = RHI::ClearValue::CreateVector4Float(1.f, 0.f, 0.f, 1.f);
            bind.m_action.m_loadAction  = RHI::AttachmentLoadAction::Clear;
            bind.m_action.m_storeAction = RHI::AttachmentStoreAction::Store;

            builder.ImportImageAttachment<SPARK_PASS_TAG("UIPass")>(
                RHI::AttachmentId("SwapChain"), bind);
        };

        uiPassFunc.m_compileFunction = [&](RHIContext& rhiContext)
        {
            // pass-local compile logic only; engine handles attachment marking via PassAttachmentMarker.
        };
        
        uiPassFunc.m_executeFunction = [&](RHI::CommandList* commandList, RenderGraphExecuter&)
        {
            m_rednerUI.Render(commandList);
        };
        passContext.Add<PassFunctions>(uiPass, uiPassFunc);

    }

    void RenderSystem::InitPipeline()
    {
        auto& passContext = m_pipeline.GetPassContext();


        auto& passShaders = passContext.GetView<PassShaders>();
        for (auto& [pass, passShaders]: passShaders.each())
        {
        }

        auto renderPasses = passContext.GetView<RenderPassTag>();

        for (Pass pass: renderPasses)
        {
            RHI::PipelineStateDescriptorForDraw desc;
            /*
            desc.m_inputStreamLayout = passContext.Get<RHI::InputStreamLayout>(pass);
            desc.m_renderTargetLayout = passContext.Get<RHI::RenderTargetLayout>(pass);
            desc.m_renderStates = passContext.Get<RHI::RenderStates>(pass);

            PassShaders& shaders = passContext.Get<PassShaders>(pass);

            Ptr<RHI::ShaderStageFunction> vertFunc = m_rhiData.m_factory->CreateShaderStageFunction(RHI::ShaderStage::Vertex);
            vertFunc->SetByteCode(shaders.m_vertexShader->GetStageBytecode(RHI::ShaderStage::Vertex)->bytecode);
            vertFunc->Finalize();
            Ptr<RHI::ShaderStageFunction> fragFunc = m_rhiData.m_factory->CreateShaderStageFunction(RHI::ShaderStage::Fragment);
            fragFunc->SetByteCode(shaders.m_fragmentShader->GetStageBytecode(RHI::ShaderStage::Fragment)->bytecode);
            fragFunc->Finalize();

            desc.m_vertexFunction = eastl::move(vertFunc);
            desc.m_fragmentFunction = eastl::move(fragFunc);
            */
        }


    }

    void RenderSystem::ExecutePipeline(Pipeline& pipeline)
    {
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);

        m_rhiData.m_commandQueuecontext.Begin();

        auto& passContext = pipeline.GetPassContext();

        auto passFuncs = passContext.GetView<PassFunctions, ActivePassTag>();

        // Build
        passFuncs.each([&](Pass pass, PassFunctions& funcs)
        {
            if (funcs.m_buildFunction)
            {
                funcs.m_buildFunction(m_renderGraphBuilder);
            }
        });

        // TopoSort
        auto allPasses = passContext.GetView<ActivePassTag>();
        eastl::vector<Pass> passSorted;
        for (auto& [pass]: allPasses.each())
        {
            passSorted.push_back(pass);
        }

        // Create Transient resources and views

        // Per-frame compile state — clear last frame's leftovers before refilling.
        passContext.Clear<PassBarriers>();
        passContext.Clear<RHI::RenderPassBeginInfo>();

        const uint32_t frameIndex = m_rhiData.m_swapChain->GetCurrentImageIndex();

        // Compile
        eastl::for_each(passSorted.begin(), passSorted.end(), [&](Pass pass)
        {
            ASSERT(passContext.Has<PassAttachmentMarker>(pass),
                "The Pass {} has not PassAttachmentMarker", passContext.Get<PassName>(pass).m_name.GetCStr());
            passContext.Get<PassAttachmentMarker>(pass).m_markFn(m_rhiContext);

            PassFunctions funcs = passContext.Get<PassFunctions>(pass);
            if (funcs.m_compileFunction)
            {
                funcs.m_compileFunction(m_rhiContext);
            }

            PassBarriers passBarriers;
            CompileImageBarriers(pass, m_rhiContext, frameIndex, passBarriers);
            CompileBufferBarriers(pass, m_rhiContext, passBarriers);
            passContext.Add<PassBarriers>(pass, eastl::move(passBarriers));

            CompileRenderPassBeginInfo(pass, m_rhiContext, frameIndex, passContext);

            m_rhiContext.Clear<AttachmentCompilingTag>();
        });

        // Frame-end cleanup of compile-only state. Resource::m_resourceState (runtime ledger)
        // continues to be the source of truth for final barrier emission below.
        m_rhiContext.Clear<ResourceStateTracker>();

        
        auto bufferAttachments = m_rhiContext.GetView<BufferPassAttachment>();
        bufferAttachments.each([&](RHIHandle handle, BufferPassAttachment& a){
            m_rhiContext.DestoryEntity(handle);
        });

        auto imageAttachments = m_rhiContext.GetView<ImagePassAttachment>();
        imageAttachments.each([&](RHIHandle handle, ImagePassAttachment& a){
            m_rhiContext.DestoryEntity(handle);
        });


        // Execute.  just for Graphics Queue
        eastl::vector<RHI::CommandList*> executeCommandLists;
        executeCommandLists.reserve(passSorted.size() + 1);
        for (Pass pass : passSorted)
        {
            RHI::CommandList* commandList = m_rhiData.m_factory->CreateCommandList(*m_rhiData.m_device, RHI::HardwareQueueClass::Graphics);

            commandList->Open();
            const PassBarriers& barriers = passContext.Get<PassBarriers>(pass);
            for (const RHI::ImageBarrier&  b : barriers.m_preImage)  
            { 
                commandList->QueueBarrier(b);
            }

            for (const RHI::BufferBarrier& b : barriers.m_preBuffer) 
            { 
                commandList->QueueBarrier(b);
            }

            commandList->FlushBarriers();

            const bool hasRenderPass = passContext.Has<RHI::RenderPassBeginInfo>(pass);
            if (hasRenderPass)
            {
                commandList->BeginRenderPass(passContext.Get<RHI::RenderPassBeginInfo>(pass));
            }

            const PassFunctions& funcs = passContext.Get<PassFunctions>(pass);
            if (funcs.m_executeFunction)
            {
                funcs.m_executeFunction(commandList);
            }

            if (hasRenderPass)
            {
                commandList->EndRenderPass();
            }
            commandList->Close();

            executeCommandLists.push_back(commandList);
        }

        // Frame-end final barriers: walk Imported resources, compare each Resource's runtime
        // state against ImportedResourceState.m_final, emit a barrier for the gap.
        // Stage info is unavailable from Resource itself — use Any for safety; final barriers
        // are few and not perf-critical.
        RHI::CommandList* finalCmd = m_rhiData.m_factory->CreateCommandList(*m_rhiData.m_device, RHI::HardwareQueueClass::Graphics);
        bool finalCmdHasWork = false;
        finalCmd->Open();
        auto importedView = m_rhiContext.GetView<ImportedTag, ImportedResourceState>();
        importedView.each([&](auto resource, const ImportedResourceState& imp)
        {
            if (m_rhiContext.Has<SwapChainImages>(resource))
            {
                RHI::Image* image = m_rhiContext.Get<SwapChainImages>(resource).images[frameIndex];
                ASSERT(image != nullptr, "SwapChain image is null at frame index {}.", frameIndex);

                const RHI::ResourceState current = image->GetResourceState();
                if (current == imp.m_final) return;

                RHI::ImageBarrier b;
                b.m_image     = image;
                b.m_srcUsage  = current.m_usage;
                b.m_dstUsage  = imp.m_final.m_usage;
                b.m_srcAccess = current.m_access;
                b.m_dstAccess = imp.m_final.m_access;
                b.m_srcStage  = RHI::AttachmentStage::Any;
                b.m_dstStage  = RHI::AttachmentStage::Any;
                finalCmd->QueueBarrier(b);
                finalCmdHasWork = true;
                return;
            }
            // TODO: buffer path once a buffer-side resource component is defined.
        });

        if (finalCmdHasWork)
        {
            finalCmd->FlushBarriers();
            executeCommandLists.push_back(finalCmd);
        }
        finalCmd->Close();
        m_rhiData.m_commandQueuecontext.ExecuteCommands(RHI::HardwareQueueClass::Graphics, executeCommandLists);


        m_rhiData.m_swapChain->Present();
        m_rhiData.m_commandQueuecontext.End();

        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
    }

    void RenderSystem::InitInternal()
    {
        InitRHIData();
        InitRenderUI();
        BuildPipeline();
        InitPipeline();

        RHIExecuteContext::Push(m_rhiContext);
        TickBus::Handler::BusConnect();
    }

    void RenderSystem::ShutdownInternal()
    {
        RHIExecuteContext::Pop();
        TickBus::Handler::BusDisconnect();
    }

    void RenderSystem::OnTick(float deltaTime)
    {
        ExecutePipeline(m_pipeline);

    }
}