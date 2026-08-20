#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/vector.h>

#include <CoreComponents/Tags.h>
#include <Math/Vector2.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Drawable/GeometrySpec.h>
#include <Pass/PassAccess.h>
#include <Pass/Component/PassComponents.h>
#include <View/View.h>
#include <View/ViewTags.h>

namespace Spark::Render
{
    //! The views one pass renders. Sized for a shadow atlas's tile count so the collect
    //! call needs no allocation; fixed_vector spills to the heap past that, only slower.
    using ViewHandleList = eastl::fixed_vector<RHI::RHIHandle, 16>;

    //! What a pass can be asked to do, as a table of type-erased operations on the pass
    //! entity. Every entry is a template instantiation frozen at RenderPassBuilder::
    //! Finalize, where the pass's PassTag and its declared DrawTags / BindingTags /
    //! ViewTag are all known; runtime code (DrawItemRouter, RenderGraphExecuter,
    //! RenderSystem) then drives passes uniformly without knowing any of those types.
    //!
    //! Raw function pointers, not eastl::function: none of these own state. The
    //! capturing-callback tier is PassFunctions (the user's Build / Compile / Execute).
    //!
    //! The implementations all live below, so the whole per-pass contract reads in one
    //! file. A null entry means the pass never declared that capability.
    struct PassCapabilities
    {
        //! Does this pass consume that GeometrySpec? (.Accepts<DrawTags...>)
        bool (*m_accepts)(RHI::RHIContext&, RHI::RHIHandle drawable);

        //! Record on a freshly derived DrawItem that this pass consumes it. The stamped
        //! PassTag is what m_collectDrawItems locates the pass's draws by.
        void (*m_markDrawItem)(RHI::RHIContext&, RHI::RHIHandle drawItem);

        //! Per-frame resolve of the bindings the executer binds once per pass. Driven by
        //! RenderGraphCompiler after the pass compile hooks, so an SRG created lazily in
        //! Compile lands the same frame. (.Binds<BindingTags...>)
        void (*m_resolveSharedBindings)(RHI::RHIContext&, PassContext&, Pass);

        //! The live view instances of the type this pass renders — one DrawList each.
        //! (.RendersView<ViewTag>)
        void (*m_collectViews)(RHI::RHIContext&, ViewHandleList&);

        //! Every DrawItem this pass consumes, located by the PassTag m_markDrawItem
        //! stamps. Implied by the pass's identity, so Finalize always installs it.
        void (*m_collectDrawItems)(RHI::RHIContext&, eastl::vector<RHI::RHIHandle>&);
    };

    // ---- m_accepts -------------------------------------------------------------

    template<typename... DrawTags>
    bool AcceptDrawTags(RHI::RHIContext& ctx, RHI::RHIHandle drawable)
    {
        return (ctx.Has<DrawTags>(drawable) && ...);
    }

    // ---- m_markDrawItem --------------------------------------------------------

    template<typename PassTag>
    void MarkPassTag(RHI::RHIContext& ctx, RHI::RHIHandle drawItem)
    {
        ctx.Add<PassTag>(drawItem);
    }

    // ---- m_resolveSharedBindings -----------------------------------------------

    //! Append every ShaderBindings tagged BindingTag (a global singleton per tag) to out.
    template<typename BindingTag, size_t N>
    void ResolveSharedBinding(
        RHI::RHIContext& ctx, eastl::fixed_vector<const RHI::ShaderBindings*, N>& out)
    {
        for (auto [entity, comp] : ctx.GetView<BindingTag, RHI::Components::ShaderBindings>().each())
        {
            if (comp.m_bindings)
            {
                out.push_back(comp.m_bindings.get());
            }
        }
    }

    //! Resolve onto the pass entity what the executer binds once per pass: the pass's own
    //! per-pass bindings (space2, tagged PassTag) and the shared ones it declared.
    //!
    //! The per-pass group is injected unconditionally via PassTag (not listed in .Binds):
    //! it is definitionally the pass's own, created by GetOrCreatePassShaderBindings which
    //! stamps PassTag + Components::ShaderBindings. A pass without one resolves to empty.
    template<typename PassTag, typename... BindingTags>
    void ResolvePassSharedBindings(RHI::RHIContext& ctx, PassContext& passCtx, Pass pass)
    {
        PassSharedBindings shared;
        ResolveSharedBinding<PassTag>(ctx, shared.m_bindings);
        (ResolveSharedBinding<BindingTags>(ctx, shared.m_bindings), ...);
        passCtx.AddOrReplace<PassSharedBindings>(pass, eastl::move(shared));
    }

    // ---- m_collectViews --------------------------------------------------------

    template<typename ViewTag>
    void CollectViews(RHI::RHIContext& ctx, ViewHandleList& out)
    {
        ctx.GetView<ViewTag, View>(Exclude<DeadTag, ViewInactiveTag>).each(
            [&](RHI::RHIHandle view, const View&) { out.push_back(view); });
    }

    // ---- m_collectDrawItems ----------------------------------------------------

    //! Appends to out, which is the executer's shared per-frame arena — every pass's
    //! draws lie end to end in it, so this must never clear.
    //!
    //! Excluding DeadTag is what keeps a DrawItem marked dead this frame from being drawn
    //! one last time. The entity itself stays valid through recording: DrawItemRouter
    //! marks at RenderSystem's TICK_DEFAULT and RHIHandleClearSystem only destroys at
    //! TICK_LAST - 1, well after the render graph has executed.
    template<typename PassTag>
    void CollectPassDrawItems(RHI::RHIContext& ctx, eastl::vector<RHI::RHIHandle>& out)
    {
        ctx.GetView<PassTag, RHI::DrawItem>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle item, const RHI::DrawItem&) { out.push_back(item); });
    }
}
