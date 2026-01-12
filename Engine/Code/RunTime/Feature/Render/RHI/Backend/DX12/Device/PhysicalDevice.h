#pragma once

#include <Object/Base.h>
#include <RHI/Device/PhysicalDevice.h>

#include <DX12.h>

namespace Spark::RHI::DX12
{
    class PhysicalDevice final: public RHI::PhysicalDevice
    {
    public:
        ~PhysicalDevice() = default;

        IDXGIFactoryX* GetFactory() const;
        IDXGIAdapterX* GetAdapter() const;

    private:
        PhysicalDevice() = default;

        void Init(IDXGIFactoryX* factory, IDXGIAdapterX* adapter);
        void Shutdown() override;
        
        Ptr<IDXGIFactoryX> m_dxgiFactory;
        Ptr<IDXGIAdapterX> m_dxgiAdapter;
    };
}