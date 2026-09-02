#pragma once

#include <Object/Object.h>
#include <Base.h>

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

        ResultCode Init(PhysicalDevice& physicalDevice, const DeviceDescriptor& descriptor);

        const PhysicalDevice& GetPhysicalDevice() const;

        const DeviceDescriptor& GetDescriptor() const;

        //! Returns the set of features supported by this device.
        const DeviceFeatures& GetFeatures() const;

        //! Returns the set of hardware limits for this device.
        const DeviceLimits& GetLimits() const;

        //! Returns a union of all capabilities of a specific format.
        FormatCapabilities GetFormatCapabilities(Format format) const;

        //! The in-flight slot every per-frame resource is indexed by this frame:
        //! the copy the CPU may write and the one the frame's commands will read.
        //! ONE counter for the whole engine — a second one that merely advances at
        //! the same rate is not equivalent, since anything that rewinds one of them
        //! (SwapChain::Resize does) offsets the two permanently.
        uint32_t GetFrameIndex() const { return m_frameIndex; }

        //! Declares the slot for the frame about to be recorded. Call once per frame,
        //! before RHI::FrameEventBus broadcasts OnFrameBegin — handlers read the index
        //! from there and the bus gives them no ordering among themselves.
        void BeginFrame(uint32_t frameIndex);

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

        //! Initialize limits and resources associated with them.
        virtual ResultCode InitializeLimits() = 0;

        //! Fills the capabilities for each format.
        virtual void FillFormatsCapabilitiesInternal(FormatCapabilitiesList& formatsCapabilities) = 0;

        FormatCapabilitiesList m_formatsCapabilities;

        Ptr<PhysicalDevice> m_physicalDevice;
        bool m_isInFrame = false;
        uint32_t m_frameIndex = 0;
    };
}