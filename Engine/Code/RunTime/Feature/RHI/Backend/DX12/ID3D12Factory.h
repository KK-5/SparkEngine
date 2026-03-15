#pragma once

#include <Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Device/DeviceObjectFactory.h>

#include "Device/DeviceObjectPool.h"

#include "Resource/Buffer/Buffer.h"
#include "Resource/Buffer/BufferPool.h"
#include "Resource/Buffer/BufferView.h"
#include "Resource/Buffer/IndirectBufferSignature.h"
#include "Resource/Image/Image.h"
#include "Resource/Image/ImagePool.h"
#include "Resource/Image/ImageView.h"
#include "Resource/ShaderResource/ShaderResource.h"
#include "Resource/ShaderResource/ShaderResourcePool.h"
#include "Pipeline/PipelineLibrary.h"
#include "Pipeline/PipelineState.h"
#include "Pipeline/PipelineLayout.h"
#include "Pipeline/ShaderStageFunction.h"
#include "Fence/Fence.h"
#include "SwapChain/SwapChain.h"
#include "Resource/Sampler/Sampler.h"
#include "Command/CommandList.h"
#include "Command/CommandListPool.h"
#include "Command/CommandQueue.h"

#include "Descriptor/DescriptorContext.h"
#include "Resource/Constant/ConstantBufferContext.h"

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

        virtual CommandQueueContext& AcquireCommandQueueContext() = 0;
    };

    class ID3D12Factory final : public Service<RHI::Factory>::Handler
                              , public Service<ID3D12FactoryInterface>::Handler
    {
    public:
        RHI::ResultCode Init();

        void Shutdown();

        void Collect() override;

        ///////////////////////////////////////////////////////////
        // ID3D12FactoryInterface override
        DescriptorContext& AcquireDescriptorContext(Device& device) override;

        ConstantBufferContext& AcquireConstantBufferContext(Device& device) override;

        Ptr<PipelineLayout> CreatePipelineLayout() override;

        CommandQueueContext& AcquireCommandQueueContext() override;

        Ptr<Sampler> CreateSampler() override;
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

        Ptr<RHI::ShaderResourcePool> CreateShaderResourcePool() override;

        Ptr<RHI::PipelineLibrary> CreatePipelineLibrary() override;

        Ptr<RHI::PipelineState> CreatePipelineState() override;

        Ptr<RHI::ShaderStageFunction> CreateShaderStageFunction(RHI::ShaderStage) override;

        Ptr<RHI::Fence> CreateFence() override;

        Ptr<RHI::SwapChain> CreateSwapChain() override;
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
        DeviceObjectPool<ShaderResourcePool> m_shaderResourcePoolObjectPool;
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
    };

}