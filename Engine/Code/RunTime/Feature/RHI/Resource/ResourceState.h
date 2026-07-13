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

namespace Spark::RHI
{
    class Resource;
    class Buffer;
    class Image;

    //! Discriminator for the concrete RHI resource subtype referenced by a
    //! DeviceMemoryBarrier. Carried on the barrier itself (rather than queried
    //! from the base class) so RHI::Resource stays a thin ownership / state
    //! tracker. Producers of device memory barriers (transient pool, render-graph
    //! compiler) always know the type at the call site; backends switch on
    //! it to recover the typed pointer.
    enum class BarrierResourceType : uint8_t
    {
        Buffer,
        Image
    };

    //! Snapshot of a resource's current synchronization state, owned by
    //! `Resource::m_resourceState` and updated whenever a barrier is emitted.
    //!   - m_access : set-valued AccessFlags ("how it is being accessed")
    //!   - m_queue  : queue that most recently emitted a barrier on this
    //!                resource (= its current owner, cross-queue handoff sense)
    //!   - m_stage  : pipeline stage of the most recent barrier, fed back as
    //!                srcStage for the next one
    //!
    //! Make{Buffer,Image}Barrier auto-populate srcAccess / srcStage / srcQueue
    //! from this struct, so callers only fill the dst side.
    struct ResourceState
    {
        AccessFlags        m_access = AccessFlags::None;
        HardwareQueueClass m_queue  = HardwareQueueClass::Graphics;
        AttachmentStage    m_stage  = AttachmentStage::Any;

        bool operator==(const ResourceState& other) const
        {
            return m_access == other.m_access
                && m_queue  == other.m_queue
                && m_stage  == other.m_stage;
        }
        bool operator!=(const ResourceState& other) const { return !(*this == other); }
    };

    //! Cross-queue ownership transfer (Vulkan QFOT) is encoded by setting
    //! m_srcQueue != m_dstQueue. Such a barrier MUST be emitted twice with
    //! identical (srcQueue, dstQueue): once on the src queue's command list
    //! (release) and once on the dst queue's command list (acquire). The
    //! happens-before between release and acquire is provided externally by
    //! a timeline-semaphore signal/wait pair (see PassSyncSignal/PassSyncWait
    //! in the render layer); the barrier itself only describes the transfer.
    //!
    //! Vulkan backend: emits both halves with srcQueueFamilyIndex/dstQueueFamilyIndex
    //!     set, performs layout transition exactly as Vulkan spec requires.
    //! DX12 backend: bridges through COMMON state -- release side transitions
    //!     srcAccess -> COMMON, acquire side transitions COMMON -> dstAccess.
    //!
    //! Intra-queue is the common case: leave m_srcQueue == m_dstQueue (default).
    struct BufferBarrier
    {
        Buffer*            m_buffer    = nullptr;
        AccessFlags        m_srcAccess = AccessFlags::None;
        AccessFlags        m_dstAccess = AccessFlags::None;
        AttachmentStage    m_srcStage  = AttachmentStage::Any;
        AttachmentStage    m_dstStage  = AttachmentStage::Any;
        HardwareQueueClass m_srcQueue  = HardwareQueueClass::Graphics;
        HardwareQueueClass m_dstQueue  = HardwareQueueClass::Graphics;
    };

    struct ImageBarrier
    {
        Image*             m_image     = nullptr;
        AccessFlags        m_srcAccess = AccessFlags::None;
        AccessFlags        m_dstAccess = AccessFlags::None;
        AttachmentStage    m_srcStage  = AttachmentStage::Any;
        AttachmentStage    m_dstStage  = AttachmentStage::Any;
        HardwareQueueClass m_srcQueue  = HardwareQueueClass::Graphics;
        HardwareQueueClass m_dstQueue  = HardwareQueueClass::Graphics;
    };

    //! Heap-range coherence barrier between two resources that share backing
    //! memory. m_resourceAfter is about to be used; m_resourceBefore was the
    //! previous owner of the same heap range (null = "any prior owner").
    //!
    //! This is a memory-level concept, so Buffer/Image are not split into
    //! separate barrier types; instead m_typeBefore / m_typeAfter discriminate
    //! the concrete subtype the backend should static_cast to. Use the
    //! MakeDeviceMemoryBarrier overloads below -- they fill the type fields from
    //! the typed pointer arguments so callers cannot get them out of sync.
    //!
    //! Vulkan backend: emits a VkMemoryBarrier using m_srcStage / m_dstStage
    //!     to scope the memory hazard. m_resourceBefore / m_resourceAfter are
    //!     ignored (VkMemoryBarrier is global). The "after" image's layout
    //!     transition is carried by the regular ImageBarrier (src layout =
    //!     Undefined) that immediately follows.
    //! DX12 backend: emits a D3D12_RESOURCE_ALIASING_BARRIER from
    //!     m_resourceBefore / m_resourceAfter; then the subsequent transition
    //!     barrier handles the per-resource state.
    struct DeviceMemoryBarrier
    {
        Resource*           m_resourceBefore = nullptr;
        Resource*           m_resourceAfter  = nullptr;
        BarrierResourceType m_typeBefore     = BarrierResourceType::Buffer;
        BarrierResourceType m_typeAfter      = BarrierResourceType::Buffer;
        AttachmentStage     m_srcStage       = AttachmentStage::Any;
        AttachmentStage     m_dstStage       = AttachmentStage::Any;
    };

    //! Construct a barrier whose src side is auto-populated from
    //! `buffer.GetResourceState()` (access / stage / queue). Caller supplies the
    //! dst side. dstQueue stays at the struct default (Graphics); callers crossing
    //! queues override it post-construction.
    BufferBarrier MakeBufferBarrier(
        Buffer& buffer,
        AccessFlags dstAccess,
        AttachmentStage dstStage = AttachmentStage::Any);

    ImageBarrier MakeImageBarrier(
        Image& image,
        AccessFlags dstAccess,
        AttachmentStage dstStage = AttachmentStage::Any);

    //! Device memory barrier factories. Type fields are filled from the typed
    //! pointer arguments, so callers cannot get m_typeBefore / m_typeAfter
    //! out of sync with the actual resources. @a before may be null to
    //! denote "any prior owner"; @a after must be non-null.
    DeviceMemoryBarrier MakeDeviceMemoryBarrier(
        Buffer* before, Buffer* after,
        AttachmentStage srcStage = AttachmentStage::Any,
        AttachmentStage dstStage = AttachmentStage::Any);

    DeviceMemoryBarrier MakeDeviceMemoryBarrier(
        Buffer* before, Image* after,
        AttachmentStage srcStage = AttachmentStage::Any,
        AttachmentStage dstStage = AttachmentStage::Any);

    DeviceMemoryBarrier MakeDeviceMemoryBarrier(
        Image* before, Buffer* after,
        AttachmentStage srcStage = AttachmentStage::Any,
        AttachmentStage dstStage = AttachmentStage::Any);

    DeviceMemoryBarrier MakeDeviceMemoryBarrier(
        Image* before, Image* after,
        AttachmentStage srcStage = AttachmentStage::Any,
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

    //! Validates whether destination state is supported by the resource bind flags.
    //! Returns false and reports an error if the transition is invalid.
    bool ValidateBufferBarrier(const BufferBarrier& barrier);
    bool ValidateImageBarrier(const ImageBarrier& barrier);
}
