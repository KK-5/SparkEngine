#include "ShadowMaskSystem.h"

#include <CoreComponents/Tags.h>
#include <ECS/WorldContext.h>

#include <RHI/Command/DrawItem.h>

#include <Pass/PassTag.h>

#include "ShadowMaskLayout.h"
#include "ViewComponents.h"

namespace Spark::Render
{
    void ShadowMaskSystem::Init(RHI::RHIContext& rhiCtx)
    {
        m_draw = rhiCtx.CreateEntity();
        rhiCtx.Add<ShadowMaskDrawTag>(m_draw);
        rhiCtx.Add<SPARK_PASS_TAG("ShadowProjectionPass")>(m_draw);
        rhiCtx.Add<ShadowMaskLayout>(m_draw, ShadowMaskLayout{ 0 });
    }

    void ShadowMaskSystem::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || m_draw == RHI::NullHandle)
        {
            return;
        }

        // A light holds rows only while ShadowViewSystem has granted it tiles, so this is
        // the same test SceneBindingSystem makes when it decides m_shadowIndex.
        uint32_t granted = 0;
        world->GetView<ShadowViewRefs>(Exclude<DeadTag>).each(
            [&](Entity light, const ShadowViewRefs& refs)
        {
            if (refs.BaseIndex() < 0 || granted >= kShadowMaskCapacity)
            {
                world->Remove<ShadowMaskSlot>(light);
                return;
            }
            world->AddOrReplace<ShadowMaskSlot>(light, ShadowMaskSlot{ granted++ });
        });

        const uint32_t sliceCount =
            (granted + kShadowMaskPackWidth - 1) / kShadowMaskPackWidth;
        rhiCtx->AddOrReplace<ShadowMaskLayout>(m_draw, ShadowMaskLayout{ sliceCount });

        // With no shadowed light there is no draw, but the attachment still exists at one
        // slice: it clears to 1, which already reads as fully lit, and keeping it bound
        // every frame spares the lighting pass a sometimes-bound texture.
        if (sliceCount == 0)
        {
            rhiCtx->Remove<RHI::DrawItem>(m_draw);
            return;
        }

        RHI::DrawItem item;
        item.m_drawArguments    = RHI::DrawArguments(RHI::DrawLinear(3, 0));
        item.m_drawInstanceArgs = RHI::DrawInstanceArguments(sliceCount, 0);
        rhiCtx->AddOrReplace<RHI::DrawItem>(m_draw, eastl::move(item));
    }

    void ShadowMaskSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        rhiCtx.GetView<ShadowMaskDrawTag>().each(
            [&](RHI::RHIHandle e) { rhiCtx.DestoryEntity(e); });
        m_draw = RHI::NullHandle;
    }
}
