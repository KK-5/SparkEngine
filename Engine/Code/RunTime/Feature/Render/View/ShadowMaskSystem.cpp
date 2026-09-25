#include "ShadowMaskSystem.h"

#include <CoreComponents/Tags.h>
#include <ECS/WorldContext.h>

#include "ShadowMaskLayout.h"
#include "ViewComponents.h"

namespace Spark::Render
{
    void ShadowMaskSystem::Init(RHI::RHIContext& rhiCtx)
    {
        m_layout = rhiCtx.CreateEntity();
        rhiCtx.Add<ShadowMaskLayout>(m_layout, ShadowMaskLayout{ 0 });
    }

    void ShadowMaskSystem::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || m_layout == RHI::NullHandle)
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
        rhiCtx->AddOrReplace<ShadowMaskLayout>(m_layout, ShadowMaskLayout{ sliceCount });
    }

    void ShadowMaskSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_layout != RHI::NullHandle)
        {
            rhiCtx.DestoryEntity(m_layout);
        }
        m_layout = RHI::NullHandle;
    }
}
