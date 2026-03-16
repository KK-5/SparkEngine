#pragma once

#include <RHI/RHIInterface.h>

#include "ID3D12Factory.h"

namespace Spark::RHI::DX12
{
    class RHISystem final : public Service<RHIInterface>::Handler
    {
    public:
        void Initialize() override;

        void Shutdown() override;

        void FactoryCollect() override;

        BackendType GetBackendType() const override
        {
            return BackendType::DX12;
        }

        Factory* GetRHIFactory() const override
        {
            return m_rhiFactory.get();
        }

    private:
        eastl::unique_ptr<ID3D12Factory> m_rhiFactory;
    };
}