#include <Log/SpdLogSystem.h>
#include <Math/Vector3.h>
#include <Math/Vector2.h>
#include <Math/Matrix4x4.h>
#include <Math/MathUtils.h>
#include <Base.h>
#include <Tick/TickBus.h>

#include <Input/InputSystem.h>
#include <Input/Bus/InputEventBus.h>

#include <RHI/RHIInterface.h>
#include <RHI/Resource/ShaderResource/InputStreamLayoutBuilder.h>
#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>
#include <RHI/Resource/ShaderResource/ShaderResource.h>
#include <RHI/Resource/ShaderResource/ShaderResourceCompiler.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Attachment/RenderAttachmentLayoutBuilder.h>
#include <RHI/RenderTargetContext/RenderTargetContext.h>
#include <RHI/Command/CommandQueueContext.h>
#include <RHI/RHILimits.h>

#include <RHI/Backend/DX12/RHISystem.h>
#include <RHI/Bus/FrameEventBus.h>

#include <Resource/Asset.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/AssetManager.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderAssetCompiler.h>
#include <Resource/Common/CommonAssetLoader.h>
#include <Resource/Image/ImageAsset.h>

#include "../Common/SimpleGlfwWindow.h"


namespace Spark::SandBox
{

    struct ShapeVertex
    {
        Math::Vector3 position;
        Math::Vector2 uv;
    };

    // -------------------------------------------------------
    // Cube geometry: 24 vertices (4 per face, unique UVs)
    // -------------------------------------------------------
    static const ShapeVertex g_cubeVertices[] =
    {
        // Front face (z = +0.5)
        {{-0.5f, -0.5f,  0.5f}, {0.f, 1.f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.f, 1.f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.f, 0.f}},
        {{-0.5f,  0.5f,  0.5f}, {0.f, 0.f}},
        // Back face (z = -0.5)
        {{ 0.5f, -0.5f, -0.5f}, {0.f, 1.f}},
        {{-0.5f, -0.5f, -0.5f}, {1.f, 1.f}},
        {{-0.5f,  0.5f, -0.5f}, {1.f, 0.f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.f, 0.f}},
        // Top face (y = +0.5)
        {{-0.5f,  0.5f,  0.5f}, {0.f, 1.f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.f, 1.f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.f, 0.f}},
        {{-0.5f,  0.5f, -0.5f}, {0.f, 0.f}},
        // Bottom face (y = -0.5)
        {{-0.5f, -0.5f, -0.5f}, {0.f, 1.f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.f, 1.f}},
        {{ 0.5f, -0.5f,  0.5f}, {1.f, 0.f}},
        {{-0.5f, -0.5f,  0.5f}, {0.f, 0.f}},
        // Right face (x = +0.5)
        {{ 0.5f, -0.5f,  0.5f}, {0.f, 1.f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.f, 1.f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.f, 0.f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.f, 0.f}},
        // Left face (x = -0.5)
        {{-0.5f, -0.5f, -0.5f}, {0.f, 1.f}},
        {{-0.5f, -0.5f,  0.5f}, {1.f, 1.f}},
        {{-0.5f,  0.5f,  0.5f}, {1.f, 0.f}},
        {{-0.5f,  0.5f, -0.5f}, {0.f, 0.f}},
    };

    static const uint16_t g_cubeIndices[] =
    {
         0,  2,  1,   0,  3,  2,   // Front
         4,  6,  5,   4,  7,  6,   // Back
         8, 10,  9,   8, 11, 10,   // Top
        12, 14, 13,  12, 15, 14,   // Bottom
        16, 18, 17,  16, 19, 18,   // Right
        20, 22, 21,  20, 23, 22,   // Left
    };

    static constexpr uint32_t g_cubeIndexCount = sizeof(g_cubeIndices) / sizeof(g_cubeIndices[0]);

    class DrawShape : public Spark::Input::InputEventBus::Handler
    {
    public:
        DrawShape();
        ~DrawShape();

        void Init();
        void Run();

    private:
        void CreateDevice();
        void CreateFence();
        void CreateSwapChain();
        void CreateDepthBufferPool();
        void CreatePipelineLibrary();
        void CreateShaderResources();
        void CreatePipelineState();
        void CreateVertexBuffer();
        void CreateIndexBuffer();
        void CreateStageBuffer();
        void CreateBaseColorTexture();
        void CreateViewportAndScissor();
        void UpdateMVP();
        void BuildFrameResources();
        void BuildRenderTargetContext();
        void BuildCommandQueueContext();
        void SubmitResources(RHI::CommandList* commandList);
        void BuildCommand(RHI::CommandList* commandList);

        void LoadImageAsset();

        // InputEventBus override
        void OnWindowResizeEvent(Input::WindowResizeEvent event) override;

        Window::IWindowSystem* m_window = nullptr;

        Ptr<Resource::ImageAsset> m_imageAsset;

        // Device / Queue / Fence
        RHI::Device* m_device = nullptr;
        Ptr<RHI::SwapChain> m_swapChain;
        Ptr<RHI::Fence> m_submitFence;
        Ptr<RHI::PipelineLibrary> m_pipelineLibrary;
        Ptr<RHI::PipelineState> m_pipelineState;

        // Vertex / Index buffers (device-local)
        Ptr<RHI::BufferPool> m_bufferPool;
        Ptr<RHI::Buffer> m_vertexBuffer;
        Ptr<RHI::Buffer> m_indexBuffer;

        // Staging (upload) buffers
        Ptr<RHI::BufferPool> m_stageBufferPool;
        Ptr<RHI::Buffer> m_stageVertexBuffer;
        Ptr<RHI::Buffer> m_stageIndexBuffer;
        Ptr<RHI::Buffer> m_stageTextureBuffer;

        // Depth buffer
        Ptr<RHI::ImagePool> m_depthImagePool;

        // Swap chain images
        Ptr<RHI::ImageView> m_swapChainImageViews[2];
        RHI::Image* m_swapChainCurImage = nullptr;

        // Base color texture
        Ptr<RHI::ImagePool> m_texturePool;
        Ptr<RHI::Image> m_baseColorImage;
        Ptr<RHI::ImageView> m_baseColorImageView;

        // Shader resources (SRG)
        Ptr<RHI::ShaderResourceLayout> m_srgLayout;
        Ptr<RHI::ShaderResource> m_shaderResource;

        // Pipeline layout
        Ptr<RHI::PipelineLayoutDescriptor> m_pipelineLayoutDesc;

        // Frame Resource manager util
        RHI::RenderTargetContext m_rtContext;
        RHI::CommandQueueContext m_commandQueueContext;

        // Viewport / Scissor
        RHI::Viewport m_viewport;
        RHI::Scissor m_scissor;

        RHI::Factory* m_rhiFactory = nullptr;

        // Transform
        float m_rotationAngle = 0.f;
    };


    DrawShape::DrawShape()
    {
        m_rhiFactory = Service<Spark::RHI::RHIInterface>::Get()->GetRHIFactory();
        if (!m_rhiFactory)
        {
            LOG_ERROR("Get RHI Factory failed");
        }

        m_window = Service<Window::IWindowSystem>::Get();
        ASSERT(m_window, "Invalid window system");

        Spark::Input::InputEventBus::Handler::BusConnect(Spark::Input::InputBusId::Game);
    }

    DrawShape::~DrawShape()
    {
        Spark::Input::InputEventBus::Handler::BusDisconnect();
    }

    void DrawShape::LoadImageAsset()
    {
        Resource::AssetId shaderId("Image/rusty_metal_04_diff_2k.jpg");
        auto assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "Asset Manager is Null.");
        m_imageAsset = assetManager->LoadAsset<Resource::ImageAsset>(shaderId);
        if (m_imageAsset->GetStatus() != Resource::AssetStatus::Ready)
        {
            LOG_ERROR("Load shader asset failed.");
            return;
        }
        ASSERT(m_imageAsset->GetFormat() == Resource::ImageFormat::RGBA8, "Error image format.");
    }

    void DrawShape::CreateDevice()
    {
        auto* rhi = Service<RHI::RHIInterface>::Get();
        RHI::PhysicalDeviceList devList = rhi->EnumeratePhysicalDevices();
        ASSERT(devList.size() > 0, "No physical devices available.");
        for (Ptr<RHI::PhysicalDevice> physicalDev : devList)
        {
            LOG_INFO(physicalDev->GetDescriptor().m_description.c_str());
        }

        RHI::DeviceDescriptor desc;
        desc.m_frameCountMax = 2;
        RHI::ResultCode result = rhi->InitDevice(*devList[0], desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create Device failed!");
        }
        m_device = rhi->GetDevice();
    }

    void DrawShape::CreateFence()
    {
        m_submitFence = m_rhiFactory->CreateFence();
        RHI::ResultCode result = m_submitFence->Init(*m_device, RHI::FenceState::Reset);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create fence failed!");
        }
    }

    void DrawShape::CreateSwapChain()
    {
        m_swapChain = m_rhiFactory->CreateSwapChain();
        RHI::SwapChainDescriptor desc;
        desc.m_dimensions.m_imageCount = 2;
        desc.m_dimensions.m_imageFormat = RHI::Format::R8G8B8A8_UNORM;
        auto windowSize = m_window->GetWindowSize();
        desc.m_dimensions.m_imageHeight = windowSize.second;
        desc.m_dimensions.m_imageWidth = windowSize.first;
        desc.m_window = m_window->GetNativeHandle();
        RHI::ResultCode result = m_swapChain->Init(*m_device, m_commandQueueContext.GetCommandQueue(RHI::HardwareQueueClass::Graphics), desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create swap chain failed!");
        }

        for (uint32_t i = 0; i < desc.m_dimensions.m_imageCount; ++i)
        {
            auto image = m_swapChain->GetImage(i);
            m_swapChainImageViews[i] = m_rhiFactory->CreateImageView();
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin = 0;
            viewDesc.m_mipSliceMax = 0;
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = 0;
            result = m_swapChainImageViews[i]->Init(*image, viewDesc);
            if (result != RHI::ResultCode::Success)
            {
                LOG_ERROR("Create render target view failed!");
            }
        }
    }

    void DrawShape::CreateDepthBufferPool()
    {
        m_depthImagePool = m_rhiFactory->CreateImagePool();
        RHI::ImagePoolDescriptor poolDesc;
        poolDesc.m_bindFlags = RHI::ImageBindFlags::DepthStencil;
        RHI::ResultCode res = m_depthImagePool->Init(*m_device, poolDesc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init depth image pool failed");
            return;
        }
    }

    void DrawShape::CreatePipelineLibrary()
    {
        m_pipelineLibrary = m_rhiFactory->CreatePipelineLibrary();
        RHI::PipelineLibraryDescriptor desc;
        RHI::ResultCode result = m_pipelineLibrary->Init(*m_device, desc);
        if (result != RHI::ResultCode::Success)
        {
            LOG_ERROR("Create pipeline library failed!");
        }
    }

    void DrawShape::CreateShaderResources()
    {
        // Build ShaderResourceLayout: one constant buffer (MVP), one texture, one sampler
        m_srgLayout = m_rhiFactory->CreateShaderResourceLayout();

        // Constant: MVP matrix (4x4 float = 64 bytes)
        RHI::ShaderInputConstantDescriptor mvpConstant(
            RHI::InputName("g_MVP"),
            0,                  // byte offset
            sizeof(Math::Matrix4X4), // 64 bytes
            0,                  // register b0
            0);                 // space0
        m_srgLayout->AddShaderInput(mvpConstant);

        // Image: base color texture (t0, space0)
        RHI::ShaderInputImageDescriptor baseColorImage(
            RHI::InputName("g_BaseColor"),
            RHI::ShaderInputImageAccess::Read,
            RHI::ShaderInputImageType::Image2D,
            1,      // count
            0,      // register t0
            0);     // space0
        m_srgLayout->AddShaderInput(baseColorImage);

        // Sampler: linear wrap (s0, space0)
        RHI::ShaderInputSamplerDescriptor samplerDesc(
            RHI::InputName("g_Sampler"),
            1,      // count
            0,      // register s0
            0);     // space0
        m_srgLayout->AddShaderInput(samplerDesc);

        m_srgLayout->SetBindingSlot(0);
        bool ok = m_srgLayout->Finalize();
        ASSERT(ok, "Failed to finalize ShaderResourceLayout");

        // Create ShaderResource and init with layout
        m_shaderResource = m_rhiFactory->CreateShaderResource();
        RHI::ResultCode res = m_shaderResource->Init(*m_device, m_srgLayout);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init ShaderResource failed");
        }

        // Build PipelineLayoutDescriptor
        m_pipelineLayoutDesc = m_rhiFactory->CreatePipelineLayoutDescriptor();
        RHI::ShaderResourceBindingInfo bindingInfo;
        bindingInfo.m_constantDataBindingInfo = RHI::ResourceBindingInfo(
            RHI::ShaderStageMask::Vertex, 0, 0); // b0, space0 for vertex stage
        bindingInfo.m_resourcesRegisterMap[RHI::InputName("g_BaseColor")] =
            RHI::ResourceBindingInfo(RHI::ShaderStageMask::Fragment, 0, 0); // t0
        bindingInfo.m_resourcesRegisterMap[RHI::InputName("g_Sampler")] =
            RHI::ResourceBindingInfo(RHI::ShaderStageMask::Fragment, 0, 0); // s0
        m_pipelineLayoutDesc->AddShaderResourceLayoutInfo(*m_srgLayout, bindingInfo);
        m_pipelineLayoutDesc->Finalize();
    }

    void DrawShape::CreatePipelineState()
    {
        m_pipelineState = m_rhiFactory->CreatePipelineState();

        RHI::PipelineStateDescriptorForDraw desc;

        // InputStreamLayout: position + uv
        RHI::InputStreamLayoutBuilder builder;
        builder.Begin();
        builder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        builder.AddBuffer()->Channel("POSITION", 0, RHI::Format::R32G32B32_FLOAT)
                           ->Channel("TEXCOORD", 0, RHI::Format::R32G32_FLOAT);
        desc.m_inputStreamLayout = builder.End();

        RHI::RenderTargetLayout rtLayout;
        rtLayout.m_colorAttachmentCount = 1;
        rtLayout.m_colorFormats = {RHI::Format::R8G8B8A8_UNORM};
        rtLayout.m_depthStencilFormat = RHI::Format::D32_FLOAT;
        desc.m_renderTargetLayout = rtLayout;

        // Render states: enable depth, back-face culling
        desc.m_renderStates = RHI::RenderStates();
        desc.m_renderStates.m_depthStencilState.m_depth.m_enable = 1;
        desc.m_renderStates.m_depthStencilState.m_depth.m_writeMask = RHI::DepthWriteMask::All;
        desc.m_renderStates.m_depthStencilState.m_depth.m_func = RHI::ComparisonFunc::Less;
        desc.m_renderStates.m_depthStencilState.m_stencil.m_enable = 0;
        desc.m_renderStates.m_rasterState.m_cullMode = RHI::CullMode::Back;

        // Shader
        Resource::AssetId shaderId("Shader/DrawShape.hlsl");
        auto assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "Asset Manager is Null.");
        Ptr<Resource::Asset> assetBase = assetManager->LoadAsset(shaderId, Resource::AssetType::Shader);
        if (assetBase->GetStatus() != Resource::AssetStatus::Ready)
        {
            LOG_ERROR("Load shader asset failed.");
            return;
        }
        auto shaderData = assetBase->GetData<Resource::ShaderAssetData>();
        Ptr<RHI::ShaderStageFunction> vertFunc = m_rhiFactory->CreateShaderStageFunction(RHI::ShaderStage::Vertex);
        vertFunc->SetByteCode(shaderData->GetStageBytecode(RHI::ShaderStage::Vertex)->bytecode);
        vertFunc->Finalize();
        Ptr<RHI::ShaderStageFunction> fragFunc = m_rhiFactory->CreateShaderStageFunction(RHI::ShaderStage::Fragment);
        fragFunc->SetByteCode(shaderData->GetStageBytecode(RHI::ShaderStage::Fragment)->bytecode);
        fragFunc->Finalize();
        desc.m_vertexFunction = vertFunc;
        desc.m_fragmentFunction = fragFunc;

        // Pipeline layout
        desc.m_pipelineLayoutDescriptor = m_pipelineLayoutDesc;

        RHI::ResultCode res = m_pipelineState->Init(*m_device, desc, m_pipelineLibrary.get());
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init PSO failed");
        }
    }

    void DrawShape::CreateVertexBuffer()
    {
        m_bufferPool = m_rhiFactory->CreateBufferPool();
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Device;
        desc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::All;
        RHI::ResultCode res = m_bufferPool->Init(*m_device, desc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init vertex buffer pool failed");
            return;
        }

        m_vertexBuffer = m_rhiFactory->CreateBuffer();
        RHI::BufferDescriptor bufferDesc;
        bufferDesc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
        bufferDesc.m_byteCount = sizeof(g_cubeVertices);
        RHI::BufferInitRequest initReq;
        initReq.m_buffer = m_vertexBuffer.get();
        initReq.m_descriptor = bufferDesc;
        res = m_bufferPool->InitBuffer(initReq);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init vertex buffer failed");
        }
    }

    void DrawShape::CreateIndexBuffer()
    {
        m_indexBuffer = m_rhiFactory->CreateBuffer();
        RHI::BufferDescriptor bufferDesc;
        bufferDesc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
        bufferDesc.m_byteCount = sizeof(g_cubeIndices);
        RHI::BufferInitRequest initReq;
        initReq.m_buffer = m_indexBuffer.get();
        initReq.m_descriptor = bufferDesc;
        RHI::ResultCode res = m_bufferPool->InitBuffer(initReq);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init index buffer failed");
        }
    }

    void DrawShape::CreateStageBuffer()
    {
        m_stageBufferPool = m_rhiFactory->CreateBufferPool();
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Host;
        desc.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;
        desc.m_bindFlags = RHI::BufferBindFlags::CopyRead;
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::All;
        RHI::ResultCode res = m_stageBufferPool->Init(*m_device, desc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init stage buffer pool failed");
            return;
        }

        // Staging vertex buffer
        m_stageVertexBuffer = m_rhiFactory->CreateBuffer();
        RHI::BufferDescriptor vertStageDesc;
        vertStageDesc.m_bindFlags = RHI::BufferBindFlags::CopyRead;
        vertStageDesc.m_byteCount = sizeof(g_cubeVertices);
        RHI::BufferInitRequest vertReq;
        vertReq.m_buffer = m_stageVertexBuffer.get();
        vertReq.m_descriptor = vertStageDesc;
        vertReq.m_initialData = g_cubeVertices;
        res = m_stageBufferPool->InitBuffer(vertReq);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init stage vertex buffer failed");
        }

        // Staging index buffer
        m_stageIndexBuffer = m_rhiFactory->CreateBuffer();
        RHI::BufferDescriptor idxStageDesc;
        idxStageDesc.m_bindFlags = RHI::BufferBindFlags::CopyRead;
        idxStageDesc.m_byteCount = sizeof(g_cubeIndices);
        RHI::BufferInitRequest idxReq;
        idxReq.m_buffer = m_stageIndexBuffer.get();
        idxReq.m_descriptor = idxStageDesc;
        idxReq.m_initialData = g_cubeIndices;
        res = m_stageBufferPool->InitBuffer(idxReq);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init stage index buffer failed");
        }

        const uint32_t imageWidth = m_imageAsset->GetWidth();
        const uint32_t imageHeight = m_imageAsset->GetHeight();
        const uint32_t imageBytesPerRow = imageWidth * m_imageAsset->GetImageData()->GetBytesPerPixel();
        const uint32_t imageBytesPerRowAligned = AlignUp(imageBytesPerRow, RHI::Alignment::TexturePitch);
        const uint32_t imageTotalBytes = imageBytesPerRow * imageHeight;
        const uint32_t stageTexBytes = imageBytesPerRowAligned * imageHeight;

        m_stageTextureBuffer = m_rhiFactory->CreateBuffer();
        RHI::BufferDescriptor texStageDesc;
        texStageDesc.m_bindFlags = RHI::BufferBindFlags::CopyRead;
        texStageDesc.m_byteCount = stageTexBytes;

        // 用于上传纹理数据的Buffer对对齐有要求，需要手动Map
        RHI::BufferInitRequest texReq;
        texReq.m_buffer = m_stageTextureBuffer.get();
        texReq.m_descriptor = texStageDesc;
        res = m_stageBufferPool->InitBuffer(texReq);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init stage texture buffer failed");
        }

        RHI::BufferMapRequest mapReq;
        mapReq.m_buffer = m_stageTextureBuffer.get();
        mapReq.m_byteCount = texStageDesc.m_byteCount;
        mapReq.m_byteOffset = 0;
        RHI::BufferMapResponse mapResponse;
        res = m_stageBufferPool->MapBuffer(mapReq, mapResponse);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Map stage texture buffer failed");
        }

        RHI::MemoryCopyDest copydest;
        copydest.pData = mapResponse.m_data;
        copydest.rowPitch = imageBytesPerRowAligned;
        copydest.slicePitch = stageTexBytes;
        RHI::MemoryCopySrc copySrc;
        copySrc.pData = const_cast<uint8_t*>(m_imageAsset->GetImageData()->GetPixels().data());
        copySrc.rowPitch = imageBytesPerRow;
        copySrc.slicePitch = imageTotalBytes;
        m_stageBufferPool->MemcpySubresource(&copydest, &copySrc, imageBytesPerRow, imageHeight, 1);

        m_stageBufferPool->UnmapBuffer(*m_stageTextureBuffer);
    }

    void DrawShape::CreateBaseColorTexture()
    {
        m_texturePool = m_rhiFactory->CreateImagePool();
        RHI::ImagePoolDescriptor poolDesc;
        poolDesc.m_bindFlags = RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite;
        RHI::ResultCode res = m_texturePool->Init(*m_device, poolDesc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init texture pool failed");
            return;
        }

        m_baseColorImage = m_rhiFactory->CreateImage();
        RHI::ImageInitRequest imgReq;
        imgReq.m_image = m_baseColorImage.get();
        imgReq.m_descriptor = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
            m_imageAsset->GetWidth(), m_imageAsset->GetHeight(),
            RHI::Format::R8G8B8A8_UNORM);
        res = m_texturePool->InitImage(imgReq);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init base color image failed");
            return;
        }

        m_baseColorImageView = m_rhiFactory->CreateImageView();
        RHI::ImageViewDescriptor viewDesc;
        viewDesc.m_mipSliceMin = 0;
        viewDesc.m_mipSliceMax = 0;
        viewDesc.m_arraySliceMin = 0;
        viewDesc.m_arraySliceMax = 0;
        res = m_baseColorImageView->Init(*m_baseColorImage, viewDesc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Init base color image view failed");
        }

        // Bind texture and sampler to ShaderResource
        RHI::ShaderInputIndex imageIdx = m_shaderResource->FindShaderInputImageIndex(ObjectName("g_BaseColor"));
        m_shaderResource->SetImageView(imageIdx, m_baseColorImageView.get(), 0);

        RHI::ShaderInputIndex samplerIdx = m_shaderResource->FindShaderInputSamplerIndex(ObjectName("g_Sampler"));
        RHI::SamplerState sampler = RHI::SamplerState::Create(
            RHI::FilterMode::Linear,
            RHI::FilterMode::Linear,
            RHI::AddressMode::Wrap);
        m_shaderResource->SetSampler(samplerIdx, sampler, 0);
    }

    void DrawShape::CreateViewportAndScissor()
    {
        auto windowSize = m_window->GetWindowSize();
        m_viewport = RHI::Viewport(0.f, (float)windowSize.first, 0.f, (float)windowSize.second);
        m_scissor = RHI::Scissor(0.f, 0.f, (float)windowSize.first, (float)windowSize.second);
    }

    void DrawShape::OnWindowResizeEvent(Input::WindowResizeEvent event)
    {
        if (event.width <= 0 || event.height <= 0)
        {
            return;
        }

        m_commandQueueContext.FlushCommands(RHI::HardwareQueueClass::Graphics);

        RHI::SwapChainDimensions swapChainDimen;
        swapChainDimen.m_imageCount = m_swapChain->GetImageCount();
        swapChainDimen.m_imageWidth = event.width;
        swapChainDimen.m_imageHeight = event.height;
        swapChainDimen.m_imageFormat = m_swapChain->GetDescriptor().m_dimensions.m_imageFormat;
        RHI::ResultCode res = m_swapChain->Resize(swapChainDimen);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Resize swap chain failed.");
            return;
        }

        RHI::RenderTargetContextDescriptor desc;
        desc.m_bufferCount = m_swapChain->GetImageCount();

        RHI::RTAttachmentDescriptor rtDesc;
        rtDesc.m_source = RHI::RTAttachmentSource::External;
        rtDesc.m_usage = RHI::RTAttachmentUsage::RenderTarget;
        for (uint32_t i = 0; i < desc.m_bufferCount; ++i)
        {
            rtDesc.m_externalImages.push_back(m_swapChain->GetImage(i));
        }
        desc.m_attachments.push_back(rtDesc);

        RHI::RTAttachmentDescriptor depthDesc;
        depthDesc.m_source = RHI::RTAttachmentSource::Owned;
        depthDesc.m_usage = RHI::RTAttachmentUsage::DepthStencil;
        RHI::ImageDescriptor depthImageDesc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::DepthStencil,
            event.width, event.height,
            RHI::Format::D32_FLOAT);
        depthDesc.m_imageDescriptor = depthImageDesc;
        depthDesc.m_hasOptimizedClearValue = true;
        depthDesc.m_optimizedClearValue = RHI::ClearValue::CreateDepth(1.f);
        desc.m_attachments.push_back(depthDesc);

        res = m_rtContext.Resize(desc);
        if (res != RHI::ResultCode::Success)
        {
            LOG_ERROR("Resize render target context failed.");
            return;
        }
    }

    void DrawShape::UpdateMVP()
    {
        auto windowSize = m_window->GetWindowSize();
        if (windowSize.first <= 0 || windowSize.second <= 0)
        {
            return;
        }

        float aspect = (float)windowSize.first / (float)windowSize.second;

        m_rotationAngle += 0.001f;

        Math::Matrix4X4 model = Math::Rotate(
            Math::Matrix4X4Const::IDENTITY,
            m_rotationAngle,
            Math::Vector3(0.5f, 1.f, 0.f));

        Math::Matrix4X4 view = Math::LookAt(
            Math::Vector3(0.f, 0.f, -3.f),
            Math::Vector3(0.f, 0.f, 0.f),
            Math::Vector3(0.f, 1.f, 0.f));

        Math::Matrix4X4 proj = Math::PerspectiveFov(
            Math::Radians(45.f),
            aspect,
            0.1f, 100.f);

        Math::Matrix4X4 mvp = proj * view * model;

        RHI::ShaderInputIndex mvpIdx = m_shaderResource->FindShaderInputConstantIndex(ObjectName("g_MVP"));
        m_shaderResource->SetConstantRaw(mvpIdx, &mvp, sizeof(mvp));

        // Compile ShaderResource to upload constants/descriptors to GPU
        RHI::ShaderResourceCompiler& compiler = m_rhiFactory->AcquireShaderResourceCompiler(*m_device);
        RHI::ShaderResource* srgs[] = { m_shaderResource.get() };
        compiler.Compiler(eastl::span<RHI::ShaderResource*>(srgs, 1));
    }

    void DrawShape::BuildRenderTargetContext()
    {
        RHI::RenderTargetContextDescriptor desc;
        desc.m_bufferCount = m_swapChain->GetImageCount();

        RHI::RTAttachmentDescriptor rtDesc;
        rtDesc.m_source = RHI::RTAttachmentSource::External;
        rtDesc.m_usage = RHI::RTAttachmentUsage::RenderTarget;
        for (uint32_t i = 0; i < desc.m_bufferCount; ++i)
        {
            rtDesc.m_externalImages.push_back(m_swapChain->GetImage(i));
        }
        desc.m_attachments.push_back(rtDesc);

        RHI::RTAttachmentDescriptor depthDesc;
        depthDesc.m_source = RHI::RTAttachmentSource::Owned;
        depthDesc.m_usage = RHI::RTAttachmentUsage::DepthStencil;
        auto windowSize = m_window->GetWindowSize();
        RHI::ImageDescriptor depthImageDesc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::DepthStencil,
            windowSize.first, windowSize.second,
            RHI::Format::D32_FLOAT);
        depthDesc.m_imageDescriptor = depthImageDesc;
        depthDesc.m_hasOptimizedClearValue = true;
        depthDesc.m_optimizedClearValue = RHI::ClearValue::CreateDepth(1.f);
        desc.m_attachments.push_back(depthDesc);

        m_rtContext.Init(*m_device, *m_depthImagePool, desc);
    }

    void DrawShape::BuildCommandQueueContext()
    {
        if (m_commandQueueContext.Init(*m_device) != RHI::ResultCode::Success)
        {
            LOG_ERROR("BuildCommandQueueContext failed.");
        }
    }

    void DrawShape::SubmitResources(RHI::CommandList* commandList)
    {
        commandList->Open();

        // Stage buffers -> copy read
        RHI::BufferBarrier stageVertBarrier = RHI::ConvertToCopyRead(*m_stageVertexBuffer);
        commandList->QueueBarrier(stageVertBarrier);
        RHI::BufferBarrier stageIdxBarrier = RHI::ConvertToCopyRead(*m_stageIndexBuffer);
        commandList->QueueBarrier(stageIdxBarrier);
        RHI::BufferBarrier stageTexBarrier = RHI::ConvertToCopyRead(*m_stageTextureBuffer);
        commandList->QueueBarrier(stageTexBarrier);

        // Device buffers -> copy write
        RHI::BufferBarrier vertCopyBarrier = RHI::ConvertToCopyWrite(*m_vertexBuffer);
        commandList->QueueBarrier(vertCopyBarrier);
        RHI::BufferBarrier idxCopyBarrier = RHI::ConvertToCopyWrite(*m_indexBuffer);
        commandList->QueueBarrier(idxCopyBarrier);
        RHI::ImageBarrier texCopyBarrier = RHI::ConvertToImageCopyWrite(*m_baseColorImage);
        commandList->QueueBarrier(texCopyBarrier);

        commandList->FlushBarriers();

        // Copy vertex data
        RHI::CopyItem vertCopy;
        vertCopy.m_type = RHI::CopyItemType::Buffer;
        vertCopy.m_buffer.m_sourceBuffer = m_stageVertexBuffer.get();
        vertCopy.m_buffer.m_sourceOffset = 0;
        vertCopy.m_buffer.m_destinationBuffer = m_vertexBuffer.get();
        vertCopy.m_buffer.m_destinationOffset = 0;
        vertCopy.m_buffer.m_size = m_vertexBuffer->GetDescriptor().m_byteCount;
        commandList->Submit(vertCopy);

        // Copy index data
        RHI::CopyItem idxCopy;
        idxCopy.m_type = RHI::CopyItemType::Buffer;
        idxCopy.m_buffer.m_sourceBuffer = m_stageIndexBuffer.get();
        idxCopy.m_buffer.m_sourceOffset = 0;
        idxCopy.m_buffer.m_destinationBuffer = m_indexBuffer.get();
        idxCopy.m_buffer.m_destinationOffset = 0;
        idxCopy.m_buffer.m_size = m_indexBuffer->GetDescriptor().m_byteCount;
        commandList->Submit(idxCopy);

        /// Copy texture data
        RHI::CopyItem textureCopy;
        textureCopy.m_type = RHI::CopyItemType::BufferToImage;
        textureCopy.m_bufferToImage.m_sourceBuffer = m_stageTextureBuffer.get();
        textureCopy.m_bufferToImage.m_sourceOffset = 0;
        textureCopy.m_bufferToImage.m_sourceBytesPerRow = AlignUp(m_imageAsset->GetWidth() * m_imageAsset->GetImageData()->GetBytesPerPixel(), RHI::Alignment::TexturePitch);
        textureCopy.m_bufferToImage.m_sourceBytesPerImage = textureCopy.m_bufferToImage.m_sourceBytesPerRow * m_imageAsset->GetHeight();
        textureCopy.m_bufferToImage.m_sourceFormat = RHI::Format::R8G8B8A8_UNORM;
        textureCopy.m_bufferToImage.m_sourceSize = RHI::Size(m_imageAsset->GetWidth(), m_imageAsset->GetHeight(), 1);
        textureCopy.m_bufferToImage.m_destinationImage = m_baseColorImage.get();
        textureCopy.m_bufferToImage.m_destinationSubresource = RHI::ImageSubresource(0, 0);
        textureCopy.m_bufferToImage.m_destinationOrigin = RHI::Origin(0, 0, 0);
        commandList->Submit(textureCopy);

        // Transition buffers to input assembly
        RHI::BufferBarrier vertIABarrier = RHI::ConvertToInputAssembly(*m_vertexBuffer);
        commandList->QueueBarrier(vertIABarrier);
        RHI::BufferBarrier idxIABarrier = RHI::ConvertToInputAssembly(*m_indexBuffer);
        commandList->QueueBarrier(idxIABarrier);

        // Transition base color texture to shader read
        RHI::ImageBarrier texBarrier = RHI::ConvertToImageShaderRead(*m_baseColorImage);
        commandList->QueueBarrier(texBarrier);

        commandList->FlushBarriers();

        commandList->Close();
    }

    void DrawShape::BuildCommand(RHI::CommandList* commandList)
    {
        commandList->Open();
        commandList->SetViewport(m_viewport);
        commandList->SetScissor(m_scissor);

        // Transition swap chain image to render target
        RHI::ImageBarrier rtBarrier = RHI::ConvertToRenderTarget(*m_rtContext.GetRenderTarget(0));
        commandList->QueueBarrier(rtBarrier);

        // Transition depth buffer to depth-stencil write
        RHI::ImageBarrier depthBarrier = RHI::ConvertToDepthStencilWrite(*m_rtContext.GetDepthStencil());
        commandList->QueueBarrier(depthBarrier);
        commandList->FlushBarriers();

        RHI::RenderPassBeginInfo beginInfo;
        beginInfo.m_colorAttachmentCount = 1;

        RHI::RenderPassColorAttachment renderTarget;
        renderTarget.m_view = m_rtContext.GetRenderTargetView(0);
        RHI::AttachmentLoadStoreAction loadStoreAction;
        loadStoreAction.m_clearValue = RHI::ClearValue::CreateVector4Float(0.1f, 0.1f, 0.15f, 1.f);
        loadStoreAction.m_loadAction = RHI::AttachmentLoadAction::Clear;
        loadStoreAction.m_storeAction = RHI::AttachmentStoreAction::Store;
        renderTarget.m_loadStoreAction = loadStoreAction;
        beginInfo.m_colorAttachments = {renderTarget};

        RHI::RenderPassDepthStencilAttachment depthStencil;
        depthStencil.m_view = m_rtContext.GetDepthStencilView();
        depthStencil.m_loadStoreAction = RHI::AttachmentLoadStoreAction(
            RHI::ClearValue::CreateDepth(1.f),
            RHI::AttachmentLoadAction::Clear,
            RHI::AttachmentStoreAction::Store,
            RHI::AttachmentLoadAction::DontCare,
            RHI::AttachmentStoreAction::DontCare
        );
        beginInfo.m_depthStencilAttachment = depthStencil;

        commandList->BeginRenderPass(beginInfo);

        commandList->SetPipelineState(*m_pipelineState);

        // Build draw item
        RHI::DrawItem drawItem;
        drawItem.m_drawInstanceArgs.m_instanceCount = 1;
        drawItem.m_drawInstanceArgs.m_instanceOffset = 0;

        drawItem.m_drawArguments.m_type = RHI::DrawType::Indexed;
        drawItem.m_drawArguments.m_indexed.m_indexCount = g_cubeIndexCount;
        drawItem.m_drawArguments.m_indexed.m_indexOffset = 0;
        drawItem.m_drawArguments.m_indexed.m_vertexOffset = 0;

        // Vertex buffer
        RHI::VertexInputView vertexStream(
            *m_vertexBuffer,
            0,
            m_vertexBuffer->GetDescriptor().m_byteCount,
            sizeof(ShapeVertex));
        drawItem.m_vertexBufferView.AddVertexInputView(vertexStream);

        // Index buffer
        drawItem.m_indexBufferView = RHI::IndexBufferView(
            *m_indexBuffer,
            0,
            m_indexBuffer->GetDescriptor().m_byteCount,
            RHI::IndexFormat::UINT16);

        // Shader resources
        drawItem.m_shaderResources.push_back(m_shaderResource);

        commandList->Submit(drawItem);
        commandList->EndRenderPass();

        // Transition to present
        RHI::ImageBarrier presentBarrier = RHI::ConvertToPresent(*m_rtContext.GetRenderTarget(0));
        commandList->QueueBarrier(presentBarrier);

        commandList->Close();
    }

    void DrawShape::Init()
    {
        LoadImageAsset();
        CreateDevice();
        BuildCommandQueueContext();
        CreateFence();
        CreateSwapChain();
        CreateDepthBufferPool();
        CreatePipelineLibrary();
        CreateShaderResources();
        CreatePipelineState();
        CreateVertexBuffer();
        CreateIndexBuffer();
        CreateStageBuffer();
        CreateBaseColorTexture();
        CreateViewportAndScissor();
        BuildRenderTargetContext();
    }

    void DrawShape::Run()
    {
        RHI::CommandList* commandList = m_rhiFactory->CreateCommandList(*m_device, RHI::HardwareQueueClass::Graphics);
        SubmitResources(commandList);
        RHI::CommandList* commandLists[] = { commandList };
        m_commandQueueContext.ExecuteCommands(RHI::HardwareQueueClass::Graphics, commandLists);
        m_commandQueueContext.FlushCommands(RHI::HardwareQueueClass::Graphics);

        while (!m_window->ShouldClose())
        {
            TickBus::Broadcast(&TickBus::Events::OnTick, 0.f);

            RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);
            m_rtContext.Begin();
            m_commandQueueContext.Begin();

            UpdateMVP();

            RHI::CommandList* commandList = m_rhiFactory->CreateCommandList(*m_device, RHI::HardwareQueueClass::Graphics);
            BuildCommand(commandList);
            RHI::CommandList* commandLists[] = { commandList };
            m_commandQueueContext.ExecuteCommands(RHI::HardwareQueueClass::Graphics, commandLists);

            m_swapChain->Present();

            m_commandQueueContext.End();
            m_rtContext.End();
            RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
        }
    }
}


int main(int argc, char** argv)
{
    using namespace Spark;

    LogConfig logConfig{};
    logConfig.m_showTimeStamp = true;
    UniquePtr<ILogSystem<SpdLogSystem>> s_logger = eastl::make_unique<SpdLogSystem>(logConfig);

    auto glfwWindow = CreateSystem<Spark::SandBox::SimpleGlfwWindow>(1024, 576, "DrawShape");
    glfwWindow->Init();

    auto inputSystem = CreateSystem<Spark::Input::InputSystem>();
    inputSystem->Init();

    auto rhiSystem = CreateSystem<Spark::RHI::DX12::RHISystem>();
    rhiSystem->Init();

    auto assetManager = CreateSystem<Spark::Resource::SparkAssetManager>();
    assetManager->Init();
    assetManager->AddSearchPath(SHADER_ASSET_DIR);

    Spark::SandBox::DrawShape app;

    app.Init();

    app.Run();

    return 0;
}
