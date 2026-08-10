#pragma once

#include <RHI/Context/RHIContext.h>

#include "View.h"
#include "ViewComponents.h"

namespace Spark::Render
{
    namespace Internal
    {
        //! The SRG half of a view: an entity holding a space1 ShaderBindings, deliberately
        //! carrying no view tag so a pass's .Binds<>() can never resolve to it. It is
        //! reachable only through the view entity's ViewShaderBindings, and bound per
        //! DrawList.
        //!
        //! Declared here only because CreateViewEntity is a template — not part of the
        //! surface. NullHandle if the ViewBindings layout or the SRG could not be built.
        RHI::RHIHandle CreateViewShaderBindings(RHI::RHIContext& rhiCtx);
    }

    //! A view instance: the view entity (View + ViewTag + ViewShaderBindings) plus its own
    //! space1 SRG entity. NullHandle if the SRG could not be created.
    //!
    //! A pass can only bind that SRG if its own shader declares the same space1 group, i.e.
    //! it includes ViewBindings.hlsl — binding resolves the space against the CURRENT PSO's
    //! layout, and a mismatch asserts inside BindShaderInputsForDraw.
    template<typename ViewTag>
    RHI::RHIHandle CreateViewEntity(RHI::RHIContext& rhiCtx)
    {
        const RHI::RHIHandle bindings = Internal::CreateViewShaderBindings(rhiCtx);
        if (bindings == RHI::NullHandle)
        {
            return RHI::NullHandle;
        }

        const RHI::RHIHandle view = rhiCtx.CreateEntity();
        rhiCtx.Add<ViewTag>(view);
        rhiCtx.Add<View>(view, View{});
        rhiCtx.Add<ViewShaderBindings>(view, ViewShaderBindings{ bindings });
        return view;
    }

    //! Mark the view and its SRG dead; RHIHandleClearSystem destroys them at frame end.
    void DestroyViewEntity(RHI::RHIContext& rhiCtx, RHI::RHIHandle view);
}
