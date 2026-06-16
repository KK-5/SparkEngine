#pragma once

#include <Base.h>
#include <ECS/Entity.h>
#include <Math/Vector2.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

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

    private:
        // Space0: pass-level view bindings (ViewProjection)
        Ptr<RHI::ShaderBindings> m_viewBindings;
        RHI::RHIHandle           m_viewBindingsEntity = RHI::NullHandle;
    };
}
