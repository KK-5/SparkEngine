#pragma once

#include <EASTL/fixed_vector.h>

#include <CoreComponents/Tags.h>
#include <Math/Vector2.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Pass/PassAccess.h>
#include <Pass/Component/PassComponents.h>
#include <View/View.h>
#include <View/ViewTags.h>

namespace Spark::Render
{
    //! The views one pass renders. Sized for a shadow atlas's tile count so the collect
    //! call needs no allocation; fixed_vector spills to the heap past that, only slower.
    using ViewHandleList = eastl::fixed_vector<RHI::RHIHandle, 16>;

    using ShaderBindingsList =
        eastl::fixed_vector<const RHI::ShaderBindings*, RHI::Limits::Pipeline::ShaderInputGroupCountMax>;

    //! What a pass can be asked to do, as a table of type-erased operations on the pass
    //! entity. Every entry is a template instantiation frozen at RenderPassBuilder::
    //! Finalize, where the pass's declared BindingTags / ViewTag are known; the render
    //! graph then drives passes uniformly without knowing any of those types.
    //!
    //! Raw function pointers, not eastl::function: none of these own state. The
    //! capturing-callback tier is PassFunctions (the user's Build / Compile / Execute).
    //!
    //! The implementations all live below, so the whole per-pass contract reads in one
    //! file. A null entry means the pass never declared that capability.
    struct PassCapabilities
    {
        //! Append the shared bindings this pass declared, bound once per Scope after the
        //! pass's own (PassBindings). (.Binds<BindingTags...>)
        void (*m_resolveSharedBindings)(RHI::RHIContext&, ShaderBindingsList&);

        //! The live view instances of the type this pass renders — one batch each.
        //! Null for a pass that renders no view (copy, and compute that needs no space1),
        //! which then emits a single batch. (.RendersView<ViewTag>)
        void (*m_collectViews)(RHI::RHIContext&, ViewHandleList&);
    };

    // ---- m_resolveSharedBindings -----------------------------------------------

    //! Append every ShaderBindings tagged BindingTag (a global singleton per tag) to out.
    template<typename BindingTag>
    void ResolveSharedBinding(RHI::RHIContext& ctx, ShaderBindingsList& out)
    {
        for (auto [entity, comp] : ctx.GetView<BindingTag, RHI::Components::ShaderBindings>().each())
        {
            if (comp.m_bindings)
            {
                out.push_back(comp.m_bindings.get());
            }
        }
    }

    template<typename... BindingTags>
    void ResolveSharedBindings(RHI::RHIContext& ctx, ShaderBindingsList& out)
    {
        (ResolveSharedBinding<BindingTags>(ctx, out), ...);
    }

    // ---- m_collectViews --------------------------------------------------------

    template<typename ViewTag>
    void CollectViews(RHI::RHIContext& ctx, ViewHandleList& out)
    {
        ctx.GetView<ViewTag, View>(Exclude<DeadTag, ViewInactiveTag>).each(
            [&](RHI::RHIHandle view, const View&) { out.push_back(view); });
    }
}
