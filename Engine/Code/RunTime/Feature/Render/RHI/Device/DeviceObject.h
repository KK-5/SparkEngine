#pragma once

#include <Object/Object.h>
#include "Device.h"

namespace Spark::Render::RHI
{
    class DeviceObject: public Object
    {
    public:
        virtual ~DeviceObject() = default;

        bool IsInitialized() const;

        Device& GetDevice() const;

    protected:
        DeviceObject() = default;

        void Init(Device& device);

        void Shutdown() override;
    private:
        Ptr<Device> m_device = nullptr;
    };
}