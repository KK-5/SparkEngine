#include "ViewBindingSystem.h"

#include <CoreComponents/Tags.h>

#include "View.h"
#include "ViewComponents.h"

namespace Spark::Render
{
    void ViewBindingSystem::Update(RHI::RHIContext& rhiCtx)
    {
        rhiCtx.GetView<View, ViewShaderBindings>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle, const View& view, const ViewShaderBindings& bindings)
        {
            WriteViewConstants(view, bindings.m_bindings);
        });
    }
}
