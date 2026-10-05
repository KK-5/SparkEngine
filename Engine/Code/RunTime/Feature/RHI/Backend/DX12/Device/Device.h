#pragma once

#include <EASTL/array.h>

#include <RHI/ClearValue.h>
#include <RHI/Device/Device.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

#include <3rdParty/D3D12MA/D3D12MemAlloc.h>
#include <DX12.h>
#include <ReleaseQueue.h>
#include <MemoryView.h>

#include "PhysicalDevice.h"

namespace Spark::RHI::DX12
{
    //! The record an indirect call reads; one of RHI/Command/IndirectCommands.h.
    enum class IndirectCommandType : uint8_t
    {
        Draw = 0,
        DrawIndexed,
        Dispatch,
        Count
    };

    class Device final : public RHI::Device
    {
    public:
        ID3D12DeviceX* GetDX12Device();

        const PhysicalDevice& GetPhysicalDevice() const;

        //! ExecuteIndirect wants the record layout as an object. The layouts are fixed, so
        //! there is one per type, made with the device.
        ID3D12CommandSignature* GetCommandSignature(IndirectCommandType type) const;

    private:
        //////////////////////////////
        /// RHI::Device override
        RHI::ResultCode InitInternal(RHI::PhysicalDevice& physicalDevice) override;
        void ShutdownInternal() override;
        RHI::ResultCode InitializeLimits() override;
        void FillFormatsCapabilitiesInternal(FormatCapabilitiesList& formatsCapabilities) override;
        //////////////////////////////

        /// @brief init m_features and m_limits
        void InitFeatures();

        RHI::ResultCode InitCommandSignatures();

        eastl::array<Ptr<ID3D12CommandSignature>, static_cast<size_t>(IndirectCommandType::Count)> m_commandSignatures;

        Ptr<ID3D12DeviceX> m_dx12Device;
        Ptr<IDXGIAdapterX> m_dxgiAdapter;
        Ptr<IDXGIFactoryX> m_dxgiFactory;
    };
}