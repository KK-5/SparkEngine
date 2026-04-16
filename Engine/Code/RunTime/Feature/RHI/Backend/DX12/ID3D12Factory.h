#pragma once

#include <Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Device/DeviceObjectFactory.h>
#include <RHI/Bus/FrameEventBus.h>

#include "ReleaseQueue.h"
#include "Device/DeviceObjectPool.h"
#include "Resource/Buffer/Buffer.h"
#include "Resource/Buffer/BufferPool.h"
#include "Resource/Buffer/BufferView.h"
#include "Resource/Buffer/IndirectBufferSignature.h"
#include "Resource/Image/Image.h"
#include "Resource/Image/ImagePool.h"
#include "Resource/Image/ImageView.h"
#include "Resource/ShaderResource/ShaderResource.h"
#include "Resource/ShaderResource/ShaderResourceCompiler.h"
#include "Pipeline/PipelineLibrary.h"
#include "Pipeline/PipelineState.h"
#include "Pipeline/PipelineLayout.h"
#include "Pipeline/PipelineLayoutDescriptor.h"
#include "Pipeline/ShaderStageFunction.h"
#include "Fence/Fence.h"
#include "SwapChain/SwapChain.h"
#include "Resource/Sampler/Sampler.h"
#include "Command/CommandList.h"
#include "Command/CommandListPool.h"
#include "Command/CommandQueue.h"

#include "Descriptor/DescriptorContext.h"
#include "Resource/Constant/ConstantBufferContext.h"

#include "UI/ImGui.h"

namespace Spark::RHI
{
    class SamplerState;
}

namespace Spark::RHI::DX12
{
    class Sampler;
    class DescriptorContext;
    class ConstantBufferContext;
    class PipelineLayout;
    class CommandList;
    class CommandQueueContext;
    class PhysicalDevice;
    class Device;

    // DX12内部接口
    class ID3D12FactoryInterface
    {
    public:
        virtual ~ID3D12FactoryInterface() = default;

        virtual DescriptorContext& AcquireDescriptorContext(Device& device) = 0;

        virtual ConstantBufferContext& AcquireConstantBufferContext(Device& device) = 0;

        // Sampler直接使用Factory创建
        virtual Ptr<Sampler> CreateSampler() = 0;

        virtual Ptr<PipelineLayout> CreatePipelineLayout() = 0;

        virtual void QueueForRelease(Device& device, Ptr<ID3D12Object> dx12Object) = 0;
    };

    class ID3D12Factory final : public Service<RHI::Factory>::Handler
                              , public Service<ID3D12FactoryInterface>::Handler
    {
    public:
        RHI::ResultCode Init();

        void Shutdown();

        void BeginFrame();
        
        void EndFrame();

        void Collect() override;

        ///////////////////////////////////////////////////////////
        // ID3D12FactoryInterface override
        DescriptorContext& AcquireDescriptorContext(Device& device) override;

        ConstantBufferContext& AcquireConstantBufferContext(Device& device) override;

        Ptr<PipelineLayout> CreatePipelineLayout() override;

        Ptr<Sampler> CreateSampler() override;

        void QueueForRelease(Device& device, Ptr<ID3D12Object> dx12Object) override;
        ///////////////////////////////////////////////////////////

        ///////////////////////////////////////////////////////////
        // RHI::Factory override
        RHI::PhysicalDeviceList EnumeratePhysicalDevices() override;

        RHI::CommandList* CreateCommandList(RHI::Device& device, RHI::HardwareQueueClass hardwareQueueClass) override;

        Ptr<RHI::CommandQueue> CreateCommandQueue() override;

        Ptr<RHI::Device> CreateDevice() override;

        Ptr<RHI::Buffer> CreateBuffer() override;

        Ptr<RHI::BufferPool> CreateBufferPool() override;

        Ptr<RHI::BufferView> CreateBufferView() override;

        Ptr<RHI::IndirectBufferSignature> CreateIndirectBufferSignature() override;

        Ptr<RHI::Image> CreateImage() override;

        Ptr<RHI::ImagePool> CreateImagePool() override;

        Ptr<RHI::ImageView> CreateImageView() override;

        Ptr<RHI::StreamingImagePool> CreateStreamingImagePool() override;

        Ptr<RHI::ShaderResource> CreateShaderResource() override;

        RHI::ShaderResourceCompiler& AcquireShaderResourceCompiler(RHI::Device& device) override;

        Ptr<RHI::PipelineLibrary> CreatePipelineLibrary() override;

        Ptr<RHI::PipelineState> CreatePipelineState() override;

        Ptr<RHI::ShaderStageFunction> CreateShaderStageFunction(RHI::ShaderStage) override;

        Ptr<RHI::Fence> CreateFence() override;

        Ptr<RHI::SwapChain> CreateSwapChain() override;

        Ptr<RHI::PipelineLayoutDescriptor> CreatePipelineLayoutDescriptor() override;

        UniquePtr<RHI::ImGui> CreateRHIImGui() override;
        ///////////////////////////////////////////////////////////

    private:
        Ptr<PhysicalDevice> CreatePhysicalDevice();
        Ptr<Device> CreateDX12Device();

        bool ValidateSingleDevice(Device& device);

        void InitDescriptorContext(Device& device);
        void InitConstantBufferContext(Device& device);
        void InitCommandlistAllocator(Device& device);

        // Device object pools
        DeviceObjectPool<Buffer>     m_bufferObjectPool;
        DeviceObjectPool<BufferPool> m_bufferPoolObjectPool;
        DeviceObjectPool<BufferView> m_bufferViewObjectPool;
        DeviceObjectPool<IndirectBufferSignature> m_indirectBufferSignatureObjectPool;
        DeviceObjectPool<Image>      m_imageObjectPool;
        DeviceObjectPool<ImagePool>  m_imagePoolObjectPool;
        DeviceObjectPool<ImageView>  m_imageViewObjectPool;
        DeviceObjectPool<ShaderResource> m_shaderResourceObjectPool;
        DeviceObjectPool<PipelineLibrary> m_pipelineLibraryObjectPool;
        DeviceObjectPool<PipelineState> m_pipelineStateObjectPool;
        DeviceObjectPool<Fence>      m_fenceObjectPool;
        DeviceObjectPool<SwapChain>  m_swapChainObjectPool;
        DeviceObjectPool<CommandQueue> m_commandQueueObjectPool;

        DeviceObjectPool<Sampler>    m_samplerObjectPool;
        DeviceObjectPool<PipelineLayout> m_pipelineLayoutObjectPool;

        eastl::unique_ptr<DescriptorContext> m_descriptorContext;
        eastl::unique_ptr<ConstantBufferContext> m_constantBufferContext;
        eastl::unique_ptr<CommandListAllocator> m_commandlistAllocator;
        eastl::unique_ptr<ShaderResourceCompiler> m_shaderResourceCompiler;

        eastl::unique_ptr<D3D12ObjReleaseQueue> m_dx12ObjReleaseQueue;
    };

}