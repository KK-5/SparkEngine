#pragma once

#include <RHI/Device/DeviceObject.h>

#include <DX12.h>
#include <MemoryView.h>
#include <ReleaseQueue.h>


/// @brief 专用于创建上传堆常量缓冲区
///        通常用于ShaderResource中ConstantData的资源创建，它们不需要专门的BufferPool管理
///        这里创建的Buffer可以直接Map，而BufferPool中创建的Buffer需要BufferPool::Map（可能是在默认堆上创建）
namespace Spark::RHI::DX12
{
    class Device;

    class ConstantBufferContext final : public RHI::DeviceObject
    {
    public:
        ConstantBufferContext() = default;

        RHI::ResultCode Init(Device& device);

        MemoryView CreateConstantBuffer(size_t size);

        void CollectConstantBuffer(MemoryView& memoryView);

        void Collect();

    private:
        friend class ID3D12Factory; // shutdown object

        Ptr<D3D12MA::Allocator> m_allocator;
        D3D12MAReleaseQueue m_releaseQueue;
    };
}