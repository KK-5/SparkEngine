#pragma once

#include <Service/Service.h>

#include <RHI/Factory.h>

namespace Spark::RHI
{
    class SamplerState;
}

namespace Spark::RHI::DX12
{
    class Sampler;
    class DescriptorContext;
    class ConstantBufferContext;
    class PipelineLayout;
    class CommandQueueContext;

    // DX12内部接口
    class ID3D12FactoryInterface
    {
    public:
        virtual ~ID3D12FactoryInterface() = default;

        virtual DescriptorContext& AcquireDescriptorContext() = 0;

        virtual ConstantBufferContext& AcquireConstantBufferContext() = 0;

        // Sampler直接使用Factory创建
        virtual Ptr<Sampler> AcquireSampler(RHI::SamplerState) = 0;

        virtual Ptr<PipelineLayout> CreatePipelineLayout() = 0;

        virtual CommandQueueContext& AcquireCommandQueueContext() = 0;
    };

    class ID3D12Factory final : public Service<RHI::Factory>::Handler
                              , public Service<ID3D12FactoryInterface>::Handler
    {
        // static D3D12Factory& Get();

        DescriptorContext& AcquireDescriptorContext() override;

        ConstantBufferContext& AcquireConstantBufferContext() override;

        CommandQueueContext& AcquireCommandQueueContext() override;

        Ptr<Sampler> AcquireSampler(RHI::SamplerState) override;
    };
}