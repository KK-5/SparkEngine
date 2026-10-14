#pragma once

#include <RHI/Component/EventComponent.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Context/RHIEventContext.h>
#include <RHI/Resource/AccessFlags.h>

#include <Pass/TagList.h>
#include <Pass/Component/ScopeComponents.h>

namespace Spark::Render
{
    //! A ScopeUses::Take: the uploads awaiting their acquire whose target carries Tag, each with
    //! Tag::Access. A tag for this carries the access its resources are read with.
    template<typename Tag>
    void TakeUploads(RHI::RHIContext& context, RHI::RHIEventContext& events, UploadTakeList& out)
    {
        static_assert(Tag::Access != RHI::AccessFlags::None, "A tag a Scope uses says how its resources are read.");

        auto take = [&](RHI::RHIEvent request, RHI::RHIHandle target)
        {
            if (context.Valid(target) && context.Has<Tag>(target))
            {
                out.push_back(UploadTake{ request, Tag::Access });
            }
        };
        events.GetView<RHI::UploadAcquirePendingTag, RHI::BufferUploadRequest>().each(
            [&](RHI::RHIEvent request, const RHI::BufferUploadRequest& upload) { take(request, upload.m_target); });
        events.GetView<RHI::UploadAcquirePendingTag, RHI::ImageUploadRequest>().each(
            [&](RHI::RHIEvent request, const RHI::ImageUploadRequest& upload) { take(request, upload.m_target); });
    }

    template<typename... Tags>
    void AddScopeUses([[maybe_unused]] RHI::RHIContext& context, [[maybe_unused]] RHI::RHIHandle scope, TagList<Tags...>)
    {
        (AddScopeUse(context, scope, &TakeUploads<Tags>), ...);
    }
}
