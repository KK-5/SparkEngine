#include "ViewFactory.h"

#include <CoreComponents/Tags.h>

namespace Spark::Render
{
    void DestroyViewEntity(RHI::RHIContext& rhiCtx, RHI::RHIHandle view)
    {
        if (view != RHI::NullHandle && rhiCtx.Valid(view) && !rhiCtx.Has<DeadTag>(view))
        {
            rhiCtx.Add<DeadTag>(view);
        }
    }
}
