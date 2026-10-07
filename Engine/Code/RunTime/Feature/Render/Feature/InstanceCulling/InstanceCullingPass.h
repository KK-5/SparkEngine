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
    //! Each pass creates its pair of buffers every frame. A frame in which no instance has a
    //! slot declares nothing, and they do not exist.
    struct InstanceCullingPass
    {
        //! DrawIndexedIndirectCommand records, packed from the start of the buffer.
        static const RHI::AttachmentId& OpaqueDrawArgumentsName();
        static const RHI::AttachmentId& ShadowCasterDrawArgumentsName();

        //! One uint32: how many records the list's argument buffer holds this frame.
        static const RHI::AttachmentId& OpaqueDrawCountName();
        static const RHI::AttachmentId& ShadowCasterDrawCountName();

        static void SetUp(PassContext& ctx);
    };
}
