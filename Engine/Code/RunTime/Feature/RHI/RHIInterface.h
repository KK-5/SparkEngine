#pragma once

#include <ECS/ISystem.h>

#include "Base.h"
#include "Factory.h"
#include "Bus/FrameEventBus.h"
#include "Context/RHIContext.h"
#include "Device/Device.h"
#include "Device/PhysicalDevice.h"
#include "Device/DeviceDescriptor.h"

namespace Spark::RHI
{
    //! Abstract base for an RHI backend. Concrete backends (DX12 / Vulkan / ...)
    //! derive via Service<RHIInterface>::Handler. The base owns engine-wide
    //! singletons that any RHI consumer needs:
    //!   - RHIContext   (ECS registry for RHI resource entities)
    //!   - Device       (logical device — user-driven construction, owned here)
    //!
    //! Lifecycle is split:
    //!   1. ISystem::Init() → InitInternal()
    //!      Backend creates factory, then chains to RHIInterface::InitInternal()
    //!      which pushes m_context onto the RHIExecuteContext stack.
    //!      Device is NOT created here — that's user-driven (next step).
    //!
    //!   2. Caller (e.g. RenderSystem) drives Device construction:
    //!        auto devs = rhi->EnumeratePhysicalDevices();
    //!        // ... user picks one (vendor preference, feature query, ...)
    //!        rhi->InitDevice(*devs[i], deviceDesc);
    //!      Physical-device selection and DeviceDescriptor are the user's
    //!      control points. Construction result is handed to RHIInterface,
    //!      which becomes the owner.
    //!
    //!   3. ISystem::Shutdown() → ShutdownInternal() reverses the order:
    //!      base shuts the device down, pops the context, then the backend
    //!      tears the factory down.
    //!
    //! Derived backends override InitInternal / ShutdownInternal and must
    //! explicitly chain to RHIInterface's implementation:
    //!   - InitInternal:     base call goes LAST  (context push after backend ready)
    //!   - ShutdownInternal: base call goes FIRST (device + context unwind before backend teardown)
    class RHIInterface : public ISystem
                       , public FrameEventBus::Handler
    {
    public:
        RHIInterface() = default;
        virtual ~RHIInterface() = default;

        virtual BackendType GetBackendType() const = 0;

        virtual Factory* GetRHIFactory() const = 0;

        RHIContext& GetContext() { return m_context; }

        //! Returns the engine-wide logical device, or nullptr if InitDevice
        //! has not been called yet.
        Device* GetDevice() const { return m_device.get(); }

        //! Pass-through to the backend factory's physical-device enumeration.
        //! Provided so callers can drive selection without grabbing the
        //! factory themselves.
        PhysicalDeviceList EnumeratePhysicalDevices() const;

        //! User-driven Device construction. Caller picks a PhysicalDevice
        //! from EnumeratePhysicalDevices() and supplies the DeviceDescriptor.
        //! Result is owned by RHIInterface — callers access it via GetDevice().
        //! Asserts on double-init.
        ResultCode InitDevice(PhysicalDevice& physicalDevice, const DeviceDescriptor& descriptor);

        ///////////////////////////////
        eastl::vector<HashString> Request() const override
        {
            return {};
        }

        HashString GetName() const override
        {
            return "RHI"_hs;
        }

    protected:
        //! Engine-wide RHIContext (BasicContext<RHIHandle>) — pushed in
        //! InitInternal, popped in ShutdownInternal.
        RHIContext m_context;

        //! Engine-wide logical device. Filled in by InitDevice (user-driven),
        //! shut down + released by ShutdownInternal.
        Ptr<Device> m_device;

        //! Default impl pushes m_context onto the RHIExecuteContext stack.
        //! Derived backends override and CALL THIS LAST after their own init.
        void InitInternal() override;

        //! Default impl shuts down m_device (if present) and pops m_context.
        //! Derived backends override and CALL THIS FIRST before their own teardown.
        void ShutdownInternal() override;
    };
}
