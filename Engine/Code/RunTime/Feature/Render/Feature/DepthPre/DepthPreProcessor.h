#pragma once

#include <ECS/Entity.h>
#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    class PassContext;

    class DepthPreProcessor final
    {
    public:
        struct DrawEntity
        {
            RHI::RHIHandle m_entity;
        };

        struct MatrixBindEntity
        {
            RHI::RHIHandle m_binding;
        };


        void Init(PassContext& passCtx, RHI::RHIContext& rhiCtx);
        void Shutdown(PassContext& passCtx);
        void Process(const Math::Vector2Int& renderSize);
    };
}
