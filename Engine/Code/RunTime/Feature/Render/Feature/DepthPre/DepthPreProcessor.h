#pragma once

#include <EASTL/hash_map.h>

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
        void Init(PassContext& passCtx, RHI::RHIContext& rhiCtx);
        void Shutdown(PassContext& passCtx);
        void Process(const Math::Vector2Int& renderSize);

    private:
        // Space0: pass-level view bindings (ViewProjection)
        Ptr<RHI::ShaderBindings> m_viewBindings;
        RHI::RHIHandle           m_viewBindingsEntity = RHI::NullHandle;

        // Mesh → DrawRequest entity (in RHIContext)
        eastl::hash_map<Entity, RHI::RHIHandle> m_meshToDrawRequest;

        // Mesh → per-draw ShaderBindings entity (space1, model matrix)
        eastl::hash_map<Entity, RHI::RHIHandle> m_meshToModelBinding;
    };
}
