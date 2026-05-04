/*
 * Modified by SparkEngine in 2025
 *  -- Vulkan-style resource state: (AttachmentUsage, AttachmentAccess) pair
 *     replaces D3D12-style bitmask enum. Backend derives native states from this.
 */
#pragma once

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/HardwareQueue.h>

namespace Spark::RHI
{
    class Resource;
    class Buffer;
    class Image;

    //! Discriminator for the concrete RHI resource subtype referenced by an
    //! AliasingBarrier. Carried on the barrier itself (rather than queried
    //! from the base class) so RHI::Resource stays a thin ownership / state
    //! tracker. Producers of aliasing barriers (transient pool, render-graph
    //! compiler) always know the type at the call site; backends switch on
    //! it to recover the typed pointer.
    enum class BarrierResourceType : uint8_t
    {
        Buffer,
        Image
    };

    /*
    DX12 Conversion
        (AttachmentUsage, AttachmentAccess) -> D3D12_RESOURCE_STATES
        ------------------------------------------------------------------
        (InputAssembly, Read)  → VERTEX_AND_CONSTANT_BUFFER | INDEX_BUFFER
        (Shader, Read)         → VCB | PIXEL_SR | NON_PIXEL_SR
        (Shader, Write)        → UNORDERED_ACCESS
        (Copy, Read)           → COPY_SOURCE
        (Copy, Write)          → COPY_DEST
        (DepthStencil, Read)   → DEPTH_READ
        (DepthStencil, Write)  → DEPTH_WRITE
        (RenderTarget, *)      → RENDER_TARGET
        (Indirect, *)          → INDIRECT_ARGUMENT
        (Uninitialized, *)     → COMMON
    AttachmentStage is unused in DX12
    */

    /// Tracks the current usage state of a resource.
    /// Based on Vulkan's synchronization model: Usage (what) + Access (read/write).
    struct ResourceState
    {
        AttachmentUsage  m_usage  = AttachmentUsage::Uninitialized;
        AttachmentAccess m_access = AttachmentAccess::Unknown;

        bool operator==(const ResourceState& other) const
        {
            return m_usage == other.m_usage && m_access == other.m_access;
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
    //! DX12 backend: bridges through COMMON state — release side transitions
    //!     srcUsage → COMMON, acquire side transitions COMMON → dstUsage.
    //!
    //! Intra-queue is the common case: leave m_srcQueue == m_dstQueue (default).
    struct BufferBarrier
    {
        Buffer*            m_buffer    = nullptr;
        AttachmentUsage    m_srcUsage  = AttachmentUsage::Uninitialized;
        AttachmentUsage    m_dstUsage  = AttachmentUsage::Uninitialized;
        AttachmentAccess   m_srcAccess = AttachmentAccess::Unknown;
        AttachmentAccess   m_dstAccess = AttachmentAccess::Unknown;
        AttachmentStage    m_srcStage  = AttachmentStage::Any;
        AttachmentStage    m_dstStage  = AttachmentStage::Any;
        HardwareQueueClass m_srcQueue  = HardwareQueueClass::Graphics;
        HardwareQueueClass m_dstQueue  = HardwareQueueClass::Graphics;
    };

    struct ImageBarrier
    {
        Image*             m_image     = nullptr;
        AttachmentUsage    m_srcUsage  = AttachmentUsage::Uninitialized;
        AttachmentUsage    m_dstUsage  = AttachmentUsage::Uninitialized;
        AttachmentAccess   m_srcAccess = AttachmentAccess::Unknown;
        AttachmentAccess   m_dstAccess = AttachmentAccess::Unknown;
        AttachmentStage    m_srcStage  = AttachmentStage::Any;
        AttachmentStage    m_dstStage  = AttachmentStage::Any;
        HardwareQueueClass m_srcQueue  = HardwareQueueClass::Graphics;
        HardwareQueueClass m_dstQueue  = HardwareQueueClass::Graphics;
    };

    //! Heap-range ownership transfer between two resources that share backing
    //! memory. m_resourceAfter is about to be used; m_resourceBefore was the
    //! previous owner of the same heap range (null = "any prior owner").
    //!
    //! Aliasing is a memory-level concept, so Buffer/Image are not split into
    //! separate barrier types; instead m_typeBefore / m_typeAfter discriminate
    //! the concrete subtype the backend should static_cast to. Use the
    //! MakeAliasingBarrier overloads below — they fill the type fields from
    //! the typed pointer arguments so callers cannot get them out of sync.
    //!
    //! Vulkan backend: emits a VkMemoryBarrier (or pipeline barrier) using
    //!     m_srcStage / m_dstStage to scope the memory hazard. The "after"
    //!     image's layout transition is NOT this barrier's responsibility —
    //!     it is carried by the regular ImageBarrier (src layout = Undefined)
    //!     that immediately follows the aliasing barrier.
    //! DX12 backend: emits a single D3D12_RESOURCE_ALIASING_BARRIER; stage
    //!     fields are unused (the subsequent transition barrier is enough).
    struct AliasingBarrier
    {
        Resource*           m_resourceBefore = nullptr;
        Resource*           m_resourceAfter  = nullptr;
        BarrierResourceType m_typeBefore     = BarrierResourceType::Buffer;
        BarrierResourceType m_typeAfter      = BarrierResourceType::Buffer;
        AttachmentStage     m_srcStage       = AttachmentStage::Any;
        AttachmentStage     m_dstStage       = AttachmentStage::Any;
    };

    BufferBarrier MakeBufferBarrier(
        Buffer& buffer,
        AttachmentUsage dstUsage,
        AttachmentAccess dstAccess,
        AttachmentStage srcStage = AttachmentStage::Any,
        AttachmentStage dstStage = AttachmentStage::Any);

    ImageBarrier MakeImageBarrier(
        Image& image,
        AttachmentUsage newUsage,
        AttachmentAccess dstAccess,
        AttachmentStage srcStage = AttachmentStage::Any,
        AttachmentStage dstStage = AttachmentStage::Any);

    //! Aliasing-barrier factories. Type fields are filled from the typed
    //! pointer arguments, so callers cannot get m_typeBefore / m_typeAfter
    //! out of sync with the actual resources. @a before may be null to
    //! denote "any prior owner"; @a after must be non-null.
    AliasingBarrier MakeAliasingBarrier(
        Buffer* before, Buffer* after,
        AttachmentStage srcStage = AttachmentStage::Any,
        AttachmentStage dstStage = AttachmentStage::Any);

    AliasingBarrier MakeAliasingBarrier(
        Buffer* before, Image* after,
        AttachmentStage srcStage = AttachmentStage::Any,
        AttachmentStage dstStage = AttachmentStage::Any);

    AliasingBarrier MakeAliasingBarrier(
        Image* before, Buffer* after,
        AttachmentStage srcStage = AttachmentStage::Any,
        AttachmentStage dstStage = AttachmentStage::Any);

    AliasingBarrier MakeAliasingBarrier(
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
