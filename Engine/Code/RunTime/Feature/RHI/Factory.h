#pragma once

#include <Base.h>
#include "Base.h"
#include "Device/PhysicalDevice.h"

#include "Pipeline/ShaderStages.h"
#include "HardwareQueue.h"

namespace Spark::RHI
{
    class Device;
    class DeviceObject;

    class Buffer;
    class BufferPool;
    class BufferView;

    class IndirectBufferSignature;

    class Image;
    class ImagePool;
    class ImageView;
    class StreamingImagePool;
    class TransientResourcePool;

    class ConstantsLayout;
    class ShaderResourceLayout;
    class ShaderResource;
    class ShaderResourceCompiler;
    class ShaderBindings;
    class ShaderInputCompiler;

    class PipelineLibrary;
    class PipelineState;
    class ShaderStageFunction;
    class PipelineLayoutDescriptor;

    class CommandList;
    class CommandRecorder;
    class CommandQueue;

    class Fence;

    class SwapChain;

    class ImGui;

    class Factory
    {
    public:
        Factory() = default;
        virtual ~Factory() = default;

        // Called end of per frame
        virtual void Collect() = 0;
        
        virtual PhysicalDeviceList EnumeratePhysicalDevices() = 0;

        // Commandlist创建即是初始化的，其对象生命周期由Factory管理，每帧都会重置
        // 返回指针不应该被保存
        virtual CommandList* CreateCommandList(RHI::Device& device, RHI::HardwareQueueClass hardwareQueueClass) = 0;

        // 创建独立的CommandRecorder，生命周期由调用方通过Ptr管理
        // 与池化的CommandList不同，CommandRecorder可以在帧循环外安全使用，
        // 调用方通过Reset()显式控制allocator和commandlist的重置时机
        virtual Ptr<CommandRecorder> CreateCommandRecorder() = 0;

        virtual Ptr<CommandQueue> CreateCommandQueue() = 0;

        virtual Ptr<Device> CreateDevice() = 0;

        virtual Ptr<Buffer> CreateBuffer() = 0;

        virtual Ptr<BufferPool> CreateBufferPool() = 0;

        virtual Ptr<BufferView> CreateBufferView() = 0;

        virtual Ptr<IndirectBufferSignature> CreateIndirectBufferSignature() = 0;

        virtual Ptr<Image> CreateImage() = 0;

        virtual Ptr<ImagePool> CreateImagePool() = 0;

        virtual Ptr<ImageView> CreateImageView() = 0;

        virtual Ptr<StreamingImagePool> CreateStreamingImagePool() = 0;

        virtual Ptr<TransientResourcePool> CreateTransientResourcePool() = 0;

        virtual Ptr<ShaderResource> CreateShaderResource() = 0;

        virtual ShaderResourceCompiler& AcquireShaderResourceCompiler(RHI::Device& device) = 0;

        virtual Ptr<ShaderBindings> CreateShaderBindings() = 0;

        virtual ShaderInputCompiler& AcquireShaderInputCompiler(RHI::Device& device) = 0;

        virtual Ptr<PipelineLibrary> CreatePipelineLibrary() = 0;

        virtual Ptr<PipelineState> CreatePipelineState() = 0;

        virtual Ptr<ShaderStageFunction> CreateShaderStageFunction(RHI::ShaderStage) = 0;

        virtual Ptr<Fence> CreateFence() = 0;

        virtual Ptr<SwapChain> CreateSwapChain() = 0;

        virtual Ptr<PipelineLayoutDescriptor> CreatePipelineLayoutDescriptor() = 0;

        virtual UniquePtr<ImGui> CreateRHIImGui() = 0;
        ///////////////////////////////
        // no platform backend object

        virtual Ptr<ConstantsLayout> CreateConstantsLayout();

        virtual Ptr<ShaderResourceLayout> CreateShaderResourceLayout();
    };
}