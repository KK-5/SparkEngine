#include "DrawMask.h"

namespace Spark::Render
{
    namespace
    {
        template<typename Tag>
        void AddDrawTag(RHI::RHIContext& rhiCtx, RHI::RHIHandle spec, DrawMask mask)
        {
            if (mask & DrawMaskBit<Tag>::Value)
            {
                rhiCtx.Add<Tag>(spec);
            }
        }
    }

    DrawMask DrawListsOf(const Resource::MaterialState& state)
    {
        if (Resource::IsTranslucent(state.m_alphaMode))
        {
            return DrawMaskOf<TranslucentTag>();
        }
        // Masked draws as opaque until it clips. Every opaque surface casts, until the mesh
        // itself carries an authored flag.
        return DrawMaskOf<OpaqueTag, ShadowCasterTag>();
    }

    void AddDrawTags(RHI::RHIContext& rhiCtx, RHI::RHIHandle spec, DrawMask mask)
    {
        AddDrawTag<OpaqueTag>(rhiCtx, spec, mask);
        AddDrawTag<ShadowCasterTag>(rhiCtx, spec, mask);
        AddDrawTag<TranslucentTag>(rhiCtx, spec, mask);
    }
}
