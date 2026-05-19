#pragma once

#include <Base.h>

#include <RHI/Command/CommandRecorder.h>

#include <DX12.h>

namespace Spark::RHI::DX12
{
    class Device;
    class CommandList;

    class CommandRecorder final : public RHI::CommandRecorder
    {
    public:
        RHI::ResultCode InitInternal(RHI::Device& deviceBase, const RHI::CommandRecorderDescriptor& desc) override;
        void ShutdownInternal() override;
        void ResetInternal() override;

    private:
        UniquePtr<CommandList>          m_dx12CommandList;
        ComPtr<ID3D12CommandAllocatorX> m_commandAllocator;
    };

}