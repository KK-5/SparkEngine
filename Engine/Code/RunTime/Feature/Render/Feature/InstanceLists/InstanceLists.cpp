#include "InstanceLists.h"

#include <EASTL/algorithm.h>

#include <Math/Bit.h>

#include <Binding/Instance/InstanceBinding.h>
#include <Binding/View/ViewBinding.h>

namespace Spark::Render::InstanceLists
{
    namespace
    {
        //! What the visible lists grow by, in views and in entries of one view. Powers of two.
        constexpr uint32_t ViewCapacityStep   = 4;
        constexpr uint32_t EntriesPerViewStep = 1024;
    }

    CullingLayout LayOutCulling(RHI::RHIContext& rhiContext, const ViewHandleList& views)
    {
        CullingLayout layout;
        for (auto [entity, count] : rhiContext.GetView<InstanceSlotCount>().each())
        {
            layout.m_slotCount = count.m_count;
        }

        for (RHI::RHIHandle view : views)
        {
            uint32_t viewIndex = 0;
            if (TryGetViewIndex(rhiContext, view, viewIndex))
            {
                layout.m_views.push_back(CulledView{ view, viewIndex });
            }
        }

        // Every slot may be visible in every view: each view gets room for all of them, and a
        // count of its own. Room for one view and one entry at least, so the buffers exist in
        // every frame.
        layout.m_viewCapacity = AlignUp(eastl::max(static_cast<uint32_t>(layout.m_views.size()), 1u), ViewCapacityStep);
        layout.m_maxPerView   = AlignUp(eastl::max(layout.m_slotCount, 1u), EntriesPerViewStep);
        return layout;
    }

    namespace Opaque
    {
        const RHI::AttachmentId& VisibleInstancesName()
        {
            static const RHI::AttachmentId name("OpaqueVisibleInstances");
            return name;
        }

        const RHI::AttachmentId& VisibleCountName()
        {
            static const RHI::AttachmentId name("OpaqueVisibleCount");
            return name;
        }
    }

    namespace ShadowCaster
    {
        const RHI::AttachmentId& VisibleInstancesName()
        {
            static const RHI::AttachmentId name("ShadowCasterVisibleInstances");
            return name;
        }

        const RHI::AttachmentId& VisibleCountName()
        {
            static const RHI::AttachmentId name("ShadowCasterVisibleCount");
            return name;
        }
    }

    namespace DepthPre
    {
        const RHI::AttachmentId& BinTableName()
        {
            static const RHI::AttachmentId name("DepthPreBinTable");
            return name;
        }

        const RHI::AttachmentId& DrawArgumentsName()
        {
            static const RHI::AttachmentId name("DepthPreDrawArguments");
            return name;
        }

        const RHI::AttachmentId& DrawCountName()
        {
            static const RHI::AttachmentId name("DepthPreDrawCount");
            return name;
        }
    }

    namespace GBuffer
    {
        const RHI::AttachmentId& BinTableName()
        {
            static const RHI::AttachmentId name("GBufferBinTable");
            return name;
        }

        const RHI::AttachmentId& DrawArgumentsName()
        {
            static const RHI::AttachmentId name("GBufferDrawArguments");
            return name;
        }

        const RHI::AttachmentId& DrawCountName()
        {
            static const RHI::AttachmentId name("GBufferDrawCount");
            return name;
        }
    }

    namespace Shadow
    {
        const RHI::AttachmentId& BinTableName()
        {
            static const RHI::AttachmentId name("ShadowBinTable");
            return name;
        }

        const RHI::AttachmentId& DrawArgumentsName()
        {
            static const RHI::AttachmentId name("ShadowDrawArguments");
            return name;
        }

        const RHI::AttachmentId& DrawCountName()
        {
            static const RHI::AttachmentId name("ShadowDrawCount");
            return name;
        }
    }
}
