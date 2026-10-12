#pragma once

#include <RHI/Context/RHIHandle.h>
#include <RHI/Resource/Resource.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Format.h>
#include <RHI/Origin.h>

//! What entities of RHIEventContext carry. What resource entities carry is in Component.h.
namespace Spark::RHI
{
    //////////////////////////////////////////////////////////////
    // Upload requests
    //
    // An upload is one entity, made by RequestBufferUpload / RequestImageUpload and
    // destroyed once it is over. It carries one of the state tags below. A resource may
    // have several at a time, each writing its own part.

    // The request has not been taken into a batch yet.
    struct UploadRequestedTag {};

    // The request is in a batch on the copy queue. Comes with UploadBatch.
    struct UploadingTag {};

    // The copy is over and the queue that owns the target has yet to take it back. Comes
    // with the barrier that does (BufferBarrier / ImageBarrier), which ExecuteUploadAcquires
    // records.
    struct UploadAcquirePendingTag {};

    // A buffer upload: the resource entity it writes and the CPU source data. The upload
    // thread reads m_data, so the caller keeps it valid until the upload is over.
    struct BufferUploadRequest
    {
        RHIHandle   m_target            = NullHandle;
        const void* m_data              = nullptr;
        size_t      m_dataSize          = 0;
        uint64_t    m_destinationOffset = 0;
    };

    // An image upload. Same m_data lifetime contract as BufferUploadRequest.
    // m_range specifies the subresource range to upload; per-subresource
    // layout (row pitch, image size, dimensions) is queried from the target
    // Image at execution time via GetSubresourceLayouts.
    struct ImageUploadRequest
    {
        RHIHandle             m_target            = NullHandle;
        const void*           m_data              = nullptr;
        size_t                m_dataSize          = 0;
        ImageSubresourceRange m_range {};
        Origin                m_destinationOrigin {};
        Format                m_sourceFormat      = Format::Unknown;
    };

    // The batch a request went into: its copy is over once the upload fence reaches
    // m_fenceValue. m_resource keeps the target alive for the upload thread and for the
    // barrier the request carries afterwards, and is let go of on the main thread, with
    // the request.
    struct UploadBatch
    {
        uint64_t      m_fenceValue = 0;
        Ptr<Resource> m_resource;
    };
}
