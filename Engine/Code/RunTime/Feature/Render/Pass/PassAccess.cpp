// Companion TU for the header-only PassAccess.h. Forces the header to be
// parsed as part of the SparkRender build so syntax / include errors are
// caught here rather than only at first user instantiation. Also defines
// CreatePassBindings, declared in PassBuilder.h.
#include "PassAccess.h"
#include "PassBuilder.h"

namespace Spark::Render
{
    Ptr<RHI::ShaderBindings> CreatePassBindings(PassContext& passCtx, Pass pass)
    {
        if (!passCtx.Has<PassPipelineLayout>(pass))
        {
            LOG_ERROR("[CreatePassBindings] Pass has no PassPipelineLayout "
                      "(custom-pipeline pass or shader reflection unavailable).");
            return nullptr;
        }
        auto& layout = passCtx.Get<PassPipelineLayout>(pass).m_layout;
        ASSERT(layout, "[CreatePassBindings] PassPipelineLayout component holds null layout.");

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[CreatePassBindings] RHI::RHIInterface service not registered.");

        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device,
            "[CreatePassBindings] RHI factory or device is null.");

        Ptr<RHI::ShaderBindings> bindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = kPerPassSpaceId;

        const RHI::ResultCode rc = bindings->Init(*device, desc);
        if (rc != RHI::ResultCode::Success)
        {
            LOG_ERROR("[CreatePassBindings] ShaderBindings::Init failed for spaceId={}.", kPerPassSpaceId);
            return nullptr;
        }

        passCtx.Add<PassBindings>(pass, PassBindings{ bindings });
        return bindings;
    }
}
