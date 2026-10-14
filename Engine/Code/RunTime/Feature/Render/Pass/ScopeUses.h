#pragma once

#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/AccessFlags.h>

#include <Pass/TagList.h>
#include <Pass/Component/ScopeComponents.h>

namespace Spark::Render
{
    //! A ScopeUses::Take: claims for `scope` the uploads into resources carrying Tag that await
    //! their acquire, which takes them into Tag::Access. A tag for this carries the access its
    //! resources are read with.
    template<typename Tag>
    void TakeUploads(RHI::RHIContext&, RHI::RHIHandle /*scope*/)
    {
        static_assert(Tag::Access != RHI::AccessFlags::None, "A tag a Scope uses says how its resources are read.");
        // Uploads are still acquired at the start of the frame (ExecuteUploadAcquires): none
        // awaits a claim yet.
    }

    template<typename... Tags>
    void AddScopeUses([[maybe_unused]] RHI::RHIContext& context, [[maybe_unused]] RHI::RHIHandle scope, TagList<Tags...>)
    {
        (AddScopeUse(context, scope, &TakeUploads<Tags>), ...);
    }
}
