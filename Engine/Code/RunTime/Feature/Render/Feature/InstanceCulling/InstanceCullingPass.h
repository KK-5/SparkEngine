#pragma once

#include <Base.h>

#include <RHI/Attachment/AttachmentEnums.h>

namespace Spark::Render
{
    class PassContext;

    //! Writes the lists of draws indirect calls of the scene read. Two passes, one per kind
    //! of object a pass selects: every drawable slot of g_Instances that is opaque gets a
    //! record in one list, every one that casts a shadow a record in the other. Nothing is
    //! culled yet.
    //!
    //! A list is written once per view that draws it, the opaque one for the main views and
    //! the shadow casters' for the shadow views, each into a part of the pair of buffers the
    //! pass marks as that view's (.IndirectArgumentsOf): a DrawIndirect reads its view's.
    //!
    //! Each pass creates its pair of buffers every frame, also in one where no instance has a
    //! slot or no view draws the list: the count is then 0.
    struct InstanceCullingPass
    {
        //! DrawIndexedIndirectCommand records, packed from the start of each view's part.
        static const RHI::AttachmentId& OpaqueDrawArgumentsName();
        static const RHI::AttachmentId& ShadowCasterDrawArgumentsName();

        //! A uint32 per view: how many records its part of the argument buffer holds this frame.
        static const RHI::AttachmentId& OpaqueDrawCountName();
        static const RHI::AttachmentId& ShadowCasterDrawCountName();

        static void SetUp(PassContext& ctx);
    };
}
