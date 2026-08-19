#include "ViewBindingSystem.h"

#include <CoreComponents/Tags.h>

#include <View/View.h>
#include <View/ViewComponents.h>

namespace Spark::Render
{
    void ViewBindingSystem::Update()
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }

        rhiCtx->GetView<View, ViewShaderBindings>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle, const View& view, const ViewShaderBindings& bindings)
        {
            WriteViewConstants(view, bindings.m_bindings);
        });
    }
}
