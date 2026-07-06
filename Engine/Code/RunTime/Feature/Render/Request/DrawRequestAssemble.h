#pragma once

#include <ECS/BasicContext.h>
#include <CoreComponents/Tags.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Context/RHIHandle.h>

#include <Drawable/Drawable.h>
#include <Request/DrawRequest.h>
#include <View/ViewTags.h>

namespace Spark::Render
{
    template <typename PassTag>
    void AssembleDrawRequests()
    {
        auto& ctx = *RHI::RHIExecuteContext::Current();

        // find-or-create: Drawables visible to the main view and not yet associated
        // with this Pass. Gating on Visible<MainViewTag> is the culling hook: only
        // visible world geometry is assembled. Procedural draws (skybox, full-screen
        // FX) carry no Visible<V> and manage their own DrawRequests, so they are
        // excluded here by construction. (When multi-view passes appear this template
        // gains a ViewTag parameter; today every pass is main-view.)
        ctx.GetView<DrawableTag, Visible<MainViewTag>>(Exclude<DeadTag, PassTag>)
            .each([&](RHI::RHIHandle drawable)
        {
            RHI::RHIHandle request = ctx.CreateEntity();

            DrawRequest payload;
            payload.m_drawable = drawable;

            ctx.Add<PassTag>(request);
            ctx.Add<DrawRequest>(request, eastl::move(payload));
            ctx.Add<PassTag>(drawable);
        });

        // cascade reap: DrawRequest whose Drawable is dead → DeadTag the
        // request. RHIHandleClearSystem destroys both at end of frame.
        ctx.GetView<PassTag, DrawRequest>(Exclude<DeadTag>)
            .each([&](RHI::RHIHandle request, const DrawRequest& dr)
        {
            if (dr.m_drawable == RHI::NullHandle || ctx.Has<DeadTag>(dr.m_drawable))
            {
                ctx.Add<DeadTag>(request);
            }
        });
    }
}
