#pragma once

#include <Service/Service.h>

#include <RHI/Factory.h>

namespace Spark::RHI::DX12
{
    class DescriptorContext;

    class D3D12FactoryInterface
    {
    public:
        virtual ~D3D12FactoryInterface() = default;

        virtual DescriptorContext& AcquireDescriptorContext() = 0;
    };

    class D3D12Factory final : public Service<RHI::Factory>::Handler
                             , public Service<D3D12FactoryInterface>::Handler
    {
        static D3D12Factory& Get();

        DescriptorContext& AcquireDescriptorContext() override;
    };
}