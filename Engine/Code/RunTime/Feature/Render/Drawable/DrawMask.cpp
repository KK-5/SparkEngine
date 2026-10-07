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

    DrawMask ClassifyDraw(const WorldContext& /*world*/, Entity /*entity*/)
    {
        // Everything is opaque today; becomes a per-AlphaMode split later. Orthogonal to
        // that, every opaque mesh casts, until the mesh itself carries an authored flag.
        return DrawMaskOf<OpaqueTag, ShadowCasterTag>();
    }

    void AddDrawTags(RHI::RHIContext& rhiCtx, RHI::RHIHandle spec, DrawMask mask)
    {
        AddDrawTag<OpaqueTag>(rhiCtx, spec, mask);
        AddDrawTag<ShadowCasterTag>(rhiCtx, spec, mask);
    }
}
