#include "CommandRecorder.h"

#include <Device/Device.h>
#include <Conversions.h>
#include <ID3D12Factory.h>

#include "CommandList.h"

namespace Spark::RHI::DX12
{
    RHI::ResultCode CommandRecorder::InitInternal(RHI::Device& deviceBase, const RHI::CommandRecorderDescriptor& desc)
    {
        auto& dx12Device = static_cast<Device&>(deviceBase);

        D3D12_COMMAND_LIST_TYPE type = ConvertHardwareQueueClass(desc.m_queue);

        HRESULT hr = dx12Device.GetDX12Device()->CreateCommandAllocator(
            type,
            IID_PPV_ARGS(&m_commandAllocator));

        if (!SUCCEEDED(hr))
        {
            LOG_ERROR("[CommandRecorder] CreateCommandAllocator failed.");
            return RHI::ResultCode::Fail;
        }

        m_dx12CommandList = MakeUnique<CommandList>();
        m_dx12CommandList->Init(dx12Device, desc.m_queue, m_commandAllocator.Get());

        RHI::CommandRecorder::SetCommandList(m_dx12CommandList.get());

        return RHI::ResultCode::Success;
    }

    void CommandRecorder::ShutdownInternal()
    {
        auto* factory = Service<ID3D12FactoryInterface>::Get();
        ASSERT(factory, "[CommandRecorder] ID3D12FactoryInterface is not registered.");

        static_cast<CommandListBase*>(m_dx12CommandList.get())->Shutdown();

        factory->QueueForRelease(static_cast<Device&>(GetDevice()), m_dx12CommandList->GetCommandList());
        factory->QueueForRelease(static_cast<Device&>(GetDevice()), m_commandAllocator.Get());

        m_dx12CommandList.reset();
        m_commandAllocator.Reset();
    }

    void CommandRecorder::ResetInternal()
    {
        m_commandAllocator->Reset();
        m_dx12CommandList->Reset(m_commandAllocator.Get());
    }

}