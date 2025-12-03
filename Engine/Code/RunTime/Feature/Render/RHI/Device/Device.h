#pragma once

#include <Object/Object.h>

#include <Base.h>
#include "DeviceDescriptor.h"
#include "PhysicalDevice.h"
#include "DeviceFeatures.h"
#include "DeviceLimits.h"

namespace Spark::Render::RHI
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


    };
}