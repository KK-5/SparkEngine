#pragma once

#include <Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Device/DeviceObjectFactory.h>

#include "Device/DeviceObjectPool.h"

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
    class Buffer;

    // DX12内部接口
    class ID3D12FactoryInterface
    {
    public:
        virtual ~ID3D12FactoryInterface() = default;

        virtual DescriptorContext& AcquireDescriptorContext() = 0;

        virtual ConstantBufferContext& AcquireConstantBufferContext() = 0;

        // Sampler直接使用Factory创建
        virtual Ptr<Sampler> AcquireSampler(RHI::SamplerState) = 0;

        virtual Ptr<PipelineLayout> CreatePipelineLayout() = 0;

        virtual Ptr<CommandList> CreateDX12CommandList() = 0;

        virtual CommandQueueContext& AcquireCommandQueueContext() = 0;

        virtual void QueueForRelease(Buffer* buffer);
    };

    class ID3D12Factory final : public Service<RHI::Factory>::Handler
                              , public Service<ID3D12FactoryInterface>::Handler
    {
    public:
        RHI::ResultCode Init();

        void Shutdown();

        ///////////////////////////////////////////////////////////
        // ID3D12FactoryInterface override
        DescriptorContext& AcquireDescriptorContext() override;

        ConstantBufferContext& AcquireConstantBufferContext() override;

        Ptr<CommandList> CreateDX12CommandList() override;

        CommandQueueContext& AcquireCommandQueueContext() override;

        Ptr<Sampler> AcquireSampler(RHI::SamplerState) override;

        void QueueForRelease(Buffer* buffer) override;
        ///////////////////////////////////////////////////////////

        ///////////////////////////////////////////////////////////
        // RHI::Factory onverride
        RHI::PhysicalDeviceList EnumeratePhysicalDevices() override;

        Ptr<RHI::Commandlist> CreateCommandList() override;

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

        Ptr<RHI::ShaderResourcePool> CreateShaderResourcePool() override;

        Ptr<RHI::PipelineLibrary> CreatePipelineLibrary() override;

        Ptr<RHI::PipelineState> CreatePipelineState() override;

        Ptr<RHI::ShaderStageFunction> CreateShaderStageFunction() override;

        Ptr<RHI::Fence> CreateFence() override;

        Ptr<RHI::SwapChain> CreateSwapChain() override;
        ///////////////////////////////////////////////////////////

    private:
        Ptr<PhysicalDevice> CreatePhysicalDevice();
        Ptr<Device> CreateDX12Device();

        // Device object pools
        BufferObjectPool m_bufferObjectPool;
        
    };

}