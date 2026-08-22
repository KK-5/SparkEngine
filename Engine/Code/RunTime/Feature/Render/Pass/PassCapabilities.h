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
        //! PassTag is what m_collectSubmitItems locates the pass's items by. Draw only —
        //! copy / compute producers know their own pass and stamp it themselves.
        void (*m_markSubmitItem)(RHI::RHIContext&, RHI::RHIHandle item);

        //! Per-frame resolve of the bindings the executer binds once per pass. Driven by
        //! RenderGraphCompiler after the pass compile hooks, so an SRG created lazily in
        //! Compile lands the same frame. (.Binds<BindingTags...>)
        void (*m_resolveSharedBindings)(RHI::RHIContext&, PassContext&, Pass);

        //! The live view instances of the type this pass renders — one batch each.
        //! Null for a pass that renders no view (copy, and compute that needs no space1),
        //! which then emits a single batch. (.RendersView<ViewTag>)
        void (*m_collectViews)(RHI::RHIContext&, ViewHandleList&);

        //! Every submit item this pass consumes, located by the PassTag stamped on it.
        //! Implied by the pass's identity, so Finalize always installs it. `view` is
        //! NullHandle for a viewless pass, and ignored until per-view culling lands — but
        //! it is in the signature now, because that is a change that would otherwise ripple.
        void (*m_collectSubmitItems)(RHI::RHIContext&, PassContext&, Pass,
                                     RHI::RHIHandle view, eastl::vector<RHI::RHIHandle>&);
    };

    // ---- m_accepts -------------------------------------------------------------

    template<typename... DrawTags>
    bool AcceptDrawTags(RHI::RHIContext& ctx, RHI::RHIHandle drawable)
    {
        return (ctx.Has<DrawTags>(drawable) && ...);
    }

    // ---- m_markSubmitItem ------------------------------------------------------

    template<typename PassTag>
    void MarkPassTag(RHI::RHIContext& ctx, RHI::RHIHandle item)
    {
        ctx.Add<PassTag>(item);
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

    // ---- m_collectSubmitItems --------------------------------------------------

    //! Appends to out, which is the executer's shared per-frame arena — every pass's items
    //! lie end to end in it, so this must never clear.
    //!
    //! The join on <PassTag, ItemT> is what makes the arena's type erasure safe: a wrongly
    //! tagged entity of another item type simply is not in the result, so the hook that
    //! reads it back as ItemT cannot be handed one.
    //!
    //! Excluding DeadTag is what keeps an item marked dead this frame from being submitted
    //! one last time. The entity itself stays valid through recording: DrawItemRouter
    //! marks at RenderSystem's TICK_DEFAULT and RHIHandleClearSystem only destroys at
    //! TICK_LAST - 1, well after the render graph has executed.
    template<typename PassTag, typename ItemT>
    void CollectPassItems(RHI::RHIContext& ctx, PassContext&, Pass, RHI::RHIHandle /*view*/,
                          eastl::vector<RHI::RHIHandle>& out)
    {
        ctx.GetView<PassTag, ItemT>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle item, const ItemT&) { out.push_back(item); });
    }
}
