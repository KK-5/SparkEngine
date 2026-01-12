#pragma once

#include <Object/Object.h>
#include <Object/Base.h>

#include <RHI/Base.h>
#include <RHI/Format.h>
#include "DeviceDescriptor.h"
#include "PhysicalDevice.h"
#include "DeviceFeatures.h"
#include "DeviceLimits.h"

namespace Spark::RHI
{
    class Device: public Object
    {
    public:
        virtual ~Device() = default;

        bool IsInitialized() const;

        ResultCode Init(PhysicalDevice& physicalDevice);

        ResultCode BeginFrame();

        ResultCode EndFrame();

        ResultCode WaitForIdle();

        const PhysicalDevice& GetPhysicalDevice() const;

        const DeviceDescriptor& GetDescriptor() const;

        //! Returns the set of features supported by this device.
        const DeviceFeatures& GetFeatures() const;

        //! Returns the set of hardware limits for this device.
        const DeviceLimits& GetLimits() const;

        //! Returns a union of all capabilities of a specific format.
        FormatCapabilities GetFormatCapabilities(Format format) const;

    protected:
        DeviceFeatures m_features;
        DeviceLimits m_limits;
        DeviceDescriptor m_descriptor;

        using FormatCapabilitiesList = eastl::array<FormatCapabilities, static_cast<uint32_t>(Format::Count)>;
    
    private:
        void Shutdown() override final;

        // backend
        //! Called when just the device is being initialized.
        virtual ResultCode InitInternal(PhysicalDevice& physicalDevice) = 0;

        //! Called when the device is being shutdown.
        virtual void ShutdownInternal() = 0;

        //! Called when the device is beginning a frame for processing.
        virtual ResultCode BeginFrameInternal() = 0;

        //! Called when the device is ending a frame for processing.
        virtual void EndFrameInternal() = 0;

        //! Called when the device is flushing all GPU operations and waiting for idle.
        virtual void WaitForIdleInternal() = 0;

        //! Initialize limits and resources associated with them.
        virtual ResultCode InitializeLimits() = 0;

        //! Fills the capabilities for each format.
        virtual void FillFormatsCapabilitiesInternal(FormatCapabilitiesList& formatsCapabilities) = 0;

        //! Called before the device is going to be shutdown. This lets the device release any resources
        //! that also hold on to a Ptr to Device.
        virtual void PreShutdown() = 0;

        FormatCapabilitiesList m_formatsCapabilities;

        Ptr<PhysicalDevice> m_physicalDevice;
        bool m_isInFrame = false;
    };
}