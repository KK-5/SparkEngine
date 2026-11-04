#pragma once

#include <stdexcept>

#include <EASTL/vector.h>
#include <RHI/Factory.h>
#include <RHI/Resource/Buffer/BufferPool.h>
#include <Service/Service.h>

namespace Spark::Test
{
    class FakeHostBuffer final : public RHI::Buffer
    {
    public:
        eastl::vector<uint8_t> m_bytes;
    };

    class FakeHostBufferPool final : public RHI::BufferPool
    {
        RHI::ResultCode InitInternal(RHI::Device&, const RHI::BufferPoolDescriptor&) override
        {
            return RHI::ResultCode::Success;
        }

        RHI::ResultCode InitBufferInternal(RHI::Buffer& buffer, const RHI::BufferDescriptor& descriptor) override
        {
            static_cast<FakeHostBuffer&>(buffer).m_bytes.resize(descriptor.m_byteCount);
            return RHI::ResultCode::Success;
        }

        RHI::ResultCode OrphanBufferInternal(RHI::Buffer&) override { return RHI::ResultCode::InvalidOperation; }

        RHI::ResultCode MapBufferInternal(const RHI::BufferMapRequest& request, RHI::BufferMapResponse& response) override
        {
            response.m_data = static_cast<FakeHostBuffer*>(request.m_buffer)->m_bytes.data() + request.m_byteOffset;
            return RHI::ResultCode::Success;
        }

        void UnmapBufferInternal(RHI::Buffer&) override {}
    };

    //! Only host buffers are needed: exercise the real StagingBuffer without a graphics backend.
    class FakeHostBufferFactory final : public Service<RHI::Factory>::Handler
    {
    public:
        Ptr<RHI::Buffer> CreateBuffer() override { return Ptr<RHI::Buffer>(new FakeHostBuffer); }
        Ptr<RHI::BufferPool> CreateBufferPool() override { return Ptr<RHI::BufferPool>(new FakeHostBufferPool); }

        void Collect() override {}
        RHI::PhysicalDeviceList EnumeratePhysicalDevices() override { throw std::logic_error("Unused factory method"); }
        RHI::CommandList* CreateCommandList(RHI::Device&, RHI::HardwareQueueClass) override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::CommandRecorder> CreateCommandRecorder() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::CommandQueue> CreateCommandQueue() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::Device> CreateDevice() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::BufferView> CreateBufferView() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::Image> CreateImage() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::ImagePool> CreateImagePool() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::ImageView> CreateImageView() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::StreamingImagePool> CreateStreamingImagePool() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::TransientResourcePool> CreateTransientResourcePool() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::AccelerationStructure> CreateAccelerationStructure() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::ShaderBindings> CreateShaderBindings() override { throw std::logic_error("Unused factory method"); }
        RHI::ShaderInputCompiler& AcquireShaderInputCompiler(RHI::Device&) override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::PipelineLibrary> CreatePipelineLibrary() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::PipelineState> CreatePipelineState() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::ShaderStageFunction> CreateShaderStageFunction(RHI::ShaderStage) override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::Fence> CreateFence() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::SwapChain> CreateSwapChain() override { throw std::logic_error("Unused factory method"); }
        Ptr<RHI::PipelineLayoutDescriptor> CreatePipelineLayoutDescriptor() override { throw std::logic_error("Unused factory method"); }
        UniquePtr<RHI::ImGui> CreateRHIImGui() override { throw std::logic_error("Unused factory method"); }
    };
}
