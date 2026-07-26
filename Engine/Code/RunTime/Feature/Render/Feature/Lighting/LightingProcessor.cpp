#include "LightingProcessor.h"

#include <RHI/Context/RHIContext.h>
#include <RHI/Command/DrawArguments.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>

#include <Drawable/Drawable.h>

namespace Spark::Render
{
    void LightingProcessor::Init()
    {
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        auto* passCtx = PassExecuteContext::Current();
        if (!rhiCtx || !passCtx)
        {
            return;
        }

        // Procedural full-screen triangle (DrawLinear(3)), no per-object data. Classified
        // with this pass's own PassTag: DeriveDrawItems routes it to exactly the
        // LightingPass route (.Accepts<LightingPassTag>()) and no other pass. DerivedDrawItems
        // is the reverse ref DrawableComposer teardown reaps through.
        m_drawable = rhiCtx->CreateEntity();
        Drawable drawable;
        drawable.m_drawArgs      = RHI::DrawArguments(RHI::DrawLinear(3, 0));
        drawable.m_instanceCount = 1;
        drawable.m_instanceData  = NoInstanceBinding{};
        rhiCtx->Add<Drawable>(m_drawable, eastl::move(drawable));
        rhiCtx->Add<DrawableTag>(m_drawable);
        rhiCtx->Add<SPARK_PASS_TAG("LightingPass")>(m_drawable);
        rhiCtx->Add<DerivedDrawItems>(m_drawable, DerivedDrawItems{});

        // Allocate the GBuffer SRG (space2) now so it exists before the OnTick bind loop
        // (BindPassDrawItems resolves it by PassTag). LightingPass's Compile hook fills its
        // texture slots each frame once the GBuffer is materialized.
        GetOrCreatePassShaderBindings<SPARK_PASS_TAG("LightingPass")>(*passCtx, *rhiCtx, 2);
    }
}
