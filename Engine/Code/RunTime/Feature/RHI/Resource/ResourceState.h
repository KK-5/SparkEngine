/*
 * Modified by SparkEngine in 2025
 *  -- Resource state described by an AccessFlags bitmask (set-valued, OR-able).
 *     The backend derives native states/layouts from it (see ConvertBufferState /
 *     ConvertImageState in the DX12 backend).
 */
#pragma once

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Resource/AccessFlags.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Image/ImageSubResource.h>

namespace Spark::RHI
{
    class Resource;
    class Buffer;
    class Image;

    //! Snapshot of a resource's current synchronization state, owned by the Buffer, or by
    //! the Image per subresource (ImageSubresourceStates), and updated whenever a barrier
    //! is emitted.
    //!   - m_access : set-valued AccessFlags ("how it is being accessed")
    //!   - m_queue  : queue the resource is on: the one that last recorded a barrier
    //!                on it or acquired it
    //!   - m_stage  : pipeline stage of the most recent barrier, fed back as
    //!                srcStage for the next one; none while nothing has accessed it
    //!
    //! Make{Buffer,Image}Barrier auto-populate srcAccess / srcStage from this struct,
    //! so callers only fill the dst side.
    struct ResourceState
    {
        AccessFlags        m_access = AccessFlags::None;
        HardwareQueueClass m_queue  = HardwareQueueClass::Graphics;
        AttachmentStage    m_stage  = AttachmentStage::Uninitialized;

        bool operator==(const ResourceState& other) const
        {
            return m_access == other.m_access
                && m_queue  == other.m_queue
                && m_stage  == other.m_stage;
        }
        bool operator!=(const ResourceState& other) const { return !(*this == other); }
    };

    //! A change of state on the queue whose command list records it. A resource going to
    //! another queue is a BufferQueueTransfer / ImageQueueTransfer, not a barrier.
    struct BufferBarrier
    {
        Buffer*            m_buffer    = nullptr;
        AccessFlags        m_srcAccess = AccessFlags::None;
        AccessFlags        m_dstAccess = AccessFlags::None;
        AttachmentStage    m_srcStage  = AttachmentStage::Any;
        AttachmentStage    m_dstStage  = AttachmentStage::Any;
    };

    //! The src side is the state of every subresource in m_range: subresources in different
    //! states take one barrier each.
    struct ImageBarrier
    {
        Image*                m_image     = nullptr;
        //! Defaults to the whole image.
        ImageSubresourceRange m_range;
        AccessFlags           m_srcAccess = AccessFlags::None;
        AccessFlags           m_dstAccess = AccessFlags::None;
        AttachmentStage       m_srcStage  = AttachmentStage::Any;
        AttachmentStage       m_dstStage  = AttachmentStage::Any;
    };

    //! A resource going from one queue to another. Recorded twice: CommandList::QueueRelease
    //! on m_srcQueue and QueueAcquire on m_dstQueue, in that order, which the caller sees to (a
    //! fence). The src side is the state it leaves in; the acquire takes it on to the dst side,
    //! which the release does not read. None as dst leaves it idle on the acquiring queue.
    //!
    //! A resource that is new (Initial) or whose contents need not be kept (Undefined) has
    //! nothing to hand over: the backend records no handover for it, only the acquire's change
    //! to dst.
    //!
    //! m_dstQueue binds only a resource one queue owns. One several queues share changes no
    //! owner, and whichever of them acquires it has it.
    struct BufferQueueTransfer
    {
        Buffer*            m_buffer    = nullptr;
        AccessFlags        m_srcAccess = AccessFlags::None;
        AttachmentStage    m_srcStage  = AttachmentStage::Uninitialized;
        HardwareQueueClass m_srcQueue  = HardwareQueueClass::Graphics;
        HardwareQueueClass m_dstQueue  = HardwareQueueClass::Graphics;
        AccessFlags        m_dstAccess = AccessFlags::None;
        AttachmentStage    m_dstStage  = AttachmentStage::Uninitialized;
    };

    struct ImageQueueTransfer
    {
        Image*                m_image     = nullptr;
        //! Defaults to the whole image.
        ImageSubresourceRange m_range;
        AccessFlags           m_srcAccess = AccessFlags::None;
        AttachmentStage       m_srcStage  = AttachmentStage::Uninitialized;
        HardwareQueueClass    m_srcQueue  = HardwareQueueClass::Graphics;
        HardwareQueueClass    m_dstQueue  = HardwareQueueClass::Graphics;
        AccessFlags           m_dstAccess = AccessFlags::None;
        AttachmentStage       m_dstStage  = AttachmentStage::Uninitialized;
    };

    //! Construct a barrier whose src side is auto-populated from
    //! `buffer.GetResourceState()` (access / stage). Caller supplies the dst side.
    BufferBarrier MakeBufferBarrier(
        Buffer& buffer,
        AccessFlags dstAccess,
        AttachmentStage dstStage = AttachmentStage::Any);

    //! Covers the whole image, which must be in one state.
    ImageBarrier MakeImageBarrier(
        Image& image,
        AccessFlags dstAccess,
        AttachmentStage dstStage = AttachmentStage::Any);

    // Buffer transition helpers.
    BufferBarrier ConvertToCopyRead(Buffer& buffer);
    BufferBarrier ConvertToCopyWrite(Buffer& buffer);
    BufferBarrier ConvertToShaderRead(Buffer& buffer);
    BufferBarrier ConvertToShaderWrite(Buffer& buffer);
    BufferBarrier ConvertToShaderReadWrite(Buffer& buffer);
    BufferBarrier ConvertToInputAssembly(Buffer& buffer);
    BufferBarrier ConvertToIndirect(Buffer& buffer);
    BufferBarrier ConvertToPredication(Buffer& buffer);
    BufferBarrier ConvertToRayTracingAccelerationStructure(Buffer& buffer);

    // Image transition helpers.
    ImageBarrier ConvertToRenderTarget(Image& image);
    ImageBarrier ConvertToDepthStencilRead(Image& image);
    ImageBarrier ConvertToDepthStencilWrite(Image& image);
    ImageBarrier ConvertToImageShaderRead(Image& image);
    ImageBarrier ConvertToImageShaderWrite(Image& image);
    ImageBarrier ConvertToImageShaderReadWrite(Image& image);
    ImageBarrier ConvertToImageCopyRead(Image& image);
    ImageBarrier ConvertToImageCopyWrite(Image& image);
    ImageBarrier ConvertToShadingRate(Image& image);
    ImageBarrier ConvertToPresent(Image& image);

    //! Every read the buffer's bind flags allow, and no write.
    AccessFlags GetReadOnlyAccess(const Buffer& buffer);

    //! Validates whether destination state is supported by the resource bind flags.
    //! Returns false and reports an error if the transition is invalid.
    bool ValidateBufferBarrier(const BufferBarrier& barrier);
    bool ValidateImageBarrier(const ImageBarrier& barrier);

    //! Whether a resource with this queue mask may go from srcQueue to dstQueue: the two
    //! differ, and each of them other than Copy is one of the resource's queues. Copy is a
    //! visitor: an upload leaves from it into a resource that does not list it.
    bool IsQueueTransferAllowed(
        HardwareQueueClassMask sharedMask, HardwareQueueClass srcQueue, HardwareQueueClass dstQueue);

    //! Returns false and reports an error if the transfer is invalid.
    bool ValidateBufferQueueTransfer(const BufferQueueTransfer& transfer);
    bool ValidateImageQueueTransfer(const ImageQueueTransfer& transfer);
}
