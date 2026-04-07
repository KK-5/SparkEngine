#include "ID3D12Factory.h"

#include <Log/SpdLogSystem.h>

#include "DX12.h"
#include "Device/Device.h"
#include "Device/PhysicalDevice.h"

///////////
#include <RHI/Resource/Image/StreamingImagePool.h>
///////////

namespace Spark::RHI::DX12
{
    RHI::ResultCode ID3D12Factory::Init()
    {
        m_bufferObjectPool.Init();
        m_bufferPoolObjectPool.Init();
        m_bufferViewObjectPool.Init();
        m_indirectBufferSignatureObjectPool.Init();
        m_imageObjectPool.Init();
        m_imagePoolObjectPool.Init();
        m_imageViewObjectPool.Init();
        m_shaderResourceObjectPool.Init();
        m_pipelineLibraryObjectPool.Init();
        m_pipelineStateObjectPool.Init();
        m_fenceObjectPool.Init();
        m_swapChainObjectPool.Init();
        m_commandQueueObjectPool.Init();

        m_samplerObjectPool.Init();
        m_pipelineLayoutObjectPool.Init();

        return RHI::ResultCode::Success;
    }

    void ID3D12Factory::Shutdown()
    {
        m_bufferObjectPool.Shutdown();
        m_bufferPoolObjectPool.Shutdown();
        m_bufferViewObjectPool.Shutdown();
        m_indirectBufferSignatureObjectPool.Shutdown();
        m_imageObjectPool.Shutdown();
        m_imagePoolObjectPool.Shutdown();
        m_imageViewObjectPool.Shutdown();
        m_shaderResourceObjectPool.Shutdown();
        m_pipelineLibraryObjectPool.Shutdown();
        m_pipelineStateObjectPool.Shutdown();
        m_fenceObjectPool.Shutdown();
        m_swapChainObjectPool.Shutdown();
        m_commandQueueObjectPool.Shutdown();

        m_samplerObjectPool.Shutdown();
        m_pipelineLayoutObjectPool.Shutdown();

        if (m_descriptorContext)
        {
            m_descriptorContext->Shutdown();
        }

        if (m_constantBufferContext)
        {
            m_constantBufferContext->Shutdown();
        }

        if (m_commandlistAllocator)
        {
            m_commandlistAllocator->Shutdown();
        }

        if (m_dx12ObjReleaseQueue)
        {
            m_dx12ObjReleaseQueue->Shutdown();
        }
    }

    void ID3D12Factory::Collect()
    {
        m_bufferObjectPool.Collect();
        m_bufferPoolObjectPool.Collect();
        m_bufferViewObjectPool.Collect();
        m_indirectBufferSignatureObjectPool.Collect();
        m_imageObjectPool.Collect();
        m_imagePoolObjectPool.Collect();
        m_imageViewObjectPool.Collect();
        m_shaderResourceObjectPool.Collect();
        m_pipelineLibraryObjectPool.Collect();
        m_pipelineStateObjectPool.Collect();
        m_fenceObjectPool.Collect();
        m_swapChainObjectPool.Collect();
        m_commandQueueObjectPool.Collect();

        m_samplerObjectPool.Collect();
        m_pipelineLayoutObjectPool.Collect();

        if (m_descriptorContext)
        {
            m_descriptorContext->Collect();
        }

        if (m_constantBufferContext)
        {
            m_constantBufferContext->Collect();
        }

        if (m_commandlistAllocator)
        {
            m_commandlistAllocator->Collect();
        }

        if (m_dx12ObjReleaseQueue)
        {
            m_dx12ObjReleaseQueue->Collect();
        }
    }

    bool ID3D12Factory::ValidateSingleDevice(Device& device)
    {
        static Device* singleDevice = &device;

        return singleDevice == &device;
    }

    void ID3D12Factory::InitDescriptorContext(Device& device)
    {
        m_descriptorContext = eastl::make_unique<DescriptorContext>();
        m_descriptorContext->Init(device);
    }

    void ID3D12Factory::InitConstantBufferContext(Device& device)
    {
        m_constantBufferContext = eastl::make_unique<ConstantBufferContext>();
        m_constantBufferContext->Init(device);
    }

    Ptr<PhysicalDevice> ID3D12Factory::CreatePhysicalDevice()
    {
        return new PhysicalDevice();
    }

    Ptr<Device> ID3D12Factory::CreateDX12Device()
    {
        return new Device();
    }

    DescriptorContext& ID3D12Factory::AcquireDescriptorContext(Device& device)
    {
        ASSERT(ValidateSingleDevice(device), "The current RHI system only supports a single device.");
        if (!m_descriptorContext)
        {
            InitDescriptorContext(device);
        }
        return *m_descriptorContext;
    }

    ConstantBufferContext& ID3D12Factory::AcquireConstantBufferContext(Device& device)
    {
        ASSERT(ValidateSingleDevice(device), "The current RHI system only supports a single device.");
        if (!m_constantBufferContext)
        {
            InitConstantBufferContext(device);
        }
        return *m_constantBufferContext;
    }

    void ID3D12Factory::QueueForRelease(Device& device, Ptr<ID3D12Object> dx12Object)
    {
        ASSERT(ValidateSingleDevice(device), "The current RHI system only supports a single device.");
        if (!m_dx12ObjReleaseQueue)
        {
            D3D12ObjReleaseQueue::Descriptor desc;
            desc.m_collectLatency = device.GetDescriptor().m_frameCountMax;
            desc.m_collectFunction = [&](ID3D12Object& obj){
                if (RHI::Validation::isEnabled)
                {
                    (void)obj;
                }
            };
            
            m_dx12ObjReleaseQueue = eastl::make_unique<D3D12ObjReleaseQueue>();
            m_dx12ObjReleaseQueue->Init(desc);
        }
        m_dx12ObjReleaseQueue->QueueForCollect(dx12Object);
    }

    RHI::PhysicalDeviceList ID3D12Factory::EnumeratePhysicalDevices()
    {
        RHI::PhysicalDeviceList physicalDeviceList;

        ComPtr<IDXGIFactoryX> dxgiFactory;
        HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(dxgiFactory.GetAddressOf()));

        ComPtr<IDXGIAdapter> dxgiAdapter;
        for (uint32_t i = 0; dxgiFactory->EnumAdapters(i, dxgiAdapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            ComPtr<IDXGIAdapterX> dxgiAdapterX;
            dxgiAdapter->QueryInterface(IID_PPV_ARGS(dxgiAdapterX.GetAddressOf()));

            DXGI_ADAPTER_DESC1 adapterDesc;
            dxgiAdapterX->GetDesc1(&adapterDesc);

            // Skip devices with software rasterization
            if(CheckBitsAny(adapterDesc.Flags, static_cast<UINT>(DXGI_ADAPTER_FLAG::DXGI_ADAPTER_FLAG_SOFTWARE)))
            {
                continue;
            }

            Ptr<PhysicalDevice> physicalDevice = CreatePhysicalDevice();
            physicalDevice->Init(dxgiFactory.Get(), dxgiAdapterX.Get());
            physicalDeviceList.push_back(physicalDevice);
        }

        return physicalDeviceList;
    }

    RHI::CommandList* ID3D12Factory::CreateCommandList(RHI::Device& deviceBase, RHI::HardwareQueueClass hardwareQueueClass)
    {
        Device& device = static_cast<Device&>(deviceBase);
        if (!m_commandlistAllocator)
        {
            m_commandlistAllocator = eastl::make_unique<CommandListAllocator>();
            CommandListAllocator::Descriptor desc;
            desc.m_device = &device;
            desc.m_frameCountMax = device.GetDescriptor().m_frameCountMax;
            m_commandlistAllocator->Init(desc);
        }

        return static_cast<RHI::CommandList*>(m_commandlistAllocator->Allocate(hardwareQueueClass));
    }

    Ptr<RHI::CommandQueue> ID3D12Factory::CreateCommandQueue()
    {
        return static_cast<RHI::CommandQueue*>(m_commandQueueObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::Device> ID3D12Factory::CreateDevice()
    {
        return CreateDX12Device();
    }

    Ptr<RHI::Buffer> ID3D12Factory::CreateBuffer()
    {
        return static_cast<RHI::Buffer*>(m_bufferObjectPool.CreateDeviceObject());  
    }

    Ptr<RHI::BufferPool> ID3D12Factory::CreateBufferPool()
    {
        return static_cast<RHI::BufferPool*>(m_bufferPoolObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::BufferView> ID3D12Factory::CreateBufferView()
    {
        return static_cast<RHI::BufferView*>(m_bufferViewObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::IndirectBufferSignature> ID3D12Factory::CreateIndirectBufferSignature()
    {
        return static_cast<RHI::IndirectBufferSignature*>(m_indirectBufferSignatureObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::Image> ID3D12Factory::CreateImage()
    {
        return static_cast<RHI::Image*>(m_imageObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::ImagePool> ID3D12Factory::CreateImagePool()
    {
        return static_cast<RHI::ImagePool*>(m_imagePoolObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::ImageView> ID3D12Factory::CreateImageView()
    {
        return static_cast<RHI::ImageView*>(m_imageViewObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::StreamingImagePool> ID3D12Factory::CreateStreamingImagePool()
    {
        return nullptr;
    }

    Ptr<RHI::ShaderResource> ID3D12Factory::CreateShaderResource()
    {
        return static_cast<RHI::ShaderResource*>(m_shaderResourceObjectPool.CreateDeviceObject());
    }

    RHI::ShaderResourceCompiler& ID3D12Factory::AcquireShaderResourceCompiler(RHI::Device& device)
    {
        if (!m_shaderResourceCompiler)
        {
            m_shaderResourceCompiler = eastl::make_unique<ShaderResourceCompiler>();

            Device& dx12Device = static_cast<Device&>(device);
            ASSERT(ValidateSingleDevice(dx12Device), "The current RHI system only supports a single device.");
            RHI::ShaderResourceCompilerDescriptor desc;
            m_shaderResourceCompiler->Init(dx12Device, desc);
        }

        return *m_shaderResourceCompiler;
    }

    Ptr<RHI::PipelineLibrary> ID3D12Factory::CreatePipelineLibrary()
    {
        return static_cast<RHI::PipelineLibrary*>(m_pipelineLibraryObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::PipelineState> ID3D12Factory::CreatePipelineState()
    {
        return static_cast<RHI::PipelineState*>(m_pipelineStateObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::ShaderStageFunction> ID3D12Factory::CreateShaderStageFunction(RHI::ShaderStage stage)
    {
        return new ShaderStageFunction(stage);
    }

    Ptr<RHI::Fence> ID3D12Factory::CreateFence()
    {
        return static_cast<RHI::Fence*>(m_fenceObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::SwapChain> ID3D12Factory::CreateSwapChain()
    {
        return static_cast<RHI::SwapChain*>(m_swapChainObjectPool.CreateDeviceObject());
    }

    Ptr<Sampler> ID3D12Factory::CreateSampler()
    {
        return static_cast<Sampler*>(m_samplerObjectPool.CreateDeviceObject());
    }

    Ptr<PipelineLayout> ID3D12Factory::CreatePipelineLayout()
    {
        return static_cast<PipelineLayout*>(m_pipelineLayoutObjectPool.CreateDeviceObject());
    }

    Ptr<RHI::PipelineLayoutDescriptor> ID3D12Factory::CreatePipelineLayoutDescriptor()
    {
        return static_cast<RHI::PipelineLayoutDescriptor*>(new PipelineLayoutDescriptor());
    }
}