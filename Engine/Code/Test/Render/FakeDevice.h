#pragma once

#include <RHI/Device/Device.h>
#include <RHI/Device/PhysicalDevice.h>

namespace Spark::Test
{
    //! The base Device's release queue and frame hook, with no backend: what a Suballocator
    //! reaches of a device is QueueForRelease, released at OnFrameEnd.
    class FakeDevice final : public RHI::Device
    {
    private:
        RHI::ResultCode InitInternal(RHI::PhysicalDevice&) override { return RHI::ResultCode::Success; }
        void ShutdownInternal() override {}
        RHI::ResultCode InitializeLimits() override { return RHI::ResultCode::Success; }
        void FillFormatsCapabilitiesInternal(FormatCapabilitiesList&) override {}
    };
}
