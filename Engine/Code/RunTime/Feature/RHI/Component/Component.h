#pragma once

#include <EASTL/array.h>

#include <Object/ObjectName.h>

#include <RHI/MemoryEnums.h>
#include <RHI/RHILimits.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/ShaderResource/ShaderResource.h>
#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/Format.h>
#include <RHI/Origin.h>
#include <RHI/Size.h>

namespace Spark::RHI
{
    class Fence;

    // Discovery tags
    struct ImportedTag {};
    struct TransientTag {};

    // Resource multiplicity tags — determines single vs. per-frame allocation.
    // Absence of PerFrameTag defaults to single-frame behavior.
    struct PerFrameTag {};

    // Marks an RHI resource entity whose CPU-side staging state has been mutated
    // and needs flushing this frame.
    struct RHIUpdateTag {};

    // Human-readable debug name on a resource entity.
    struct ResourceName
    {
        ObjectName m_name {};
    };

    //! Cross-system handoff handshake. Present on a resource entity when some
    //! producer has submitted work touching the resource but no cross-queue
    //! consumer has yet absorbed the fence.
    //!
    //! Producer protocol (uniform across all systems — AsyncUploadSystem, RG, ...):
    //!     after queue.Signal(myFence, V), for each touched resource:
    //!         ctx.AddOrReplace<PendingSync>(resource, {&myFence, V});
    //! Same-queue producers MUST still stamp — the next cross-queue consumer
    //! relies on the latest value being visible here.
    //!
    //! Consumer protocol (first touch of resource on a new queue):
    //!     if (resource.GetResourceState().m_queue != myQueue) {
    //!         if (auto* sync = ctx.TryGet<PendingSync>(resource)) {
    //!             myQueue.Wait(*sync->m_fence, sync->m_fenceValue);
    //!             ctx.Remove<PendingSync>(resource);
    //!         }
    //!         // emit cross-queue acquire barrier
    //!     }
    //!     // else same queue — serial execution guarantees happens-before; do nothing.
    //!
    //! AddOrReplace is safe across the various producer/consumer interleavings:
    //!  - Cross-queue overwrite: a consumer must have removed the prior PendingSync
    //!    before the new producer touched the resource, so the overwritten value
    //!    has already been consumed.
    //!  - Same-queue overwrite (possibly with different m_fence): queue serial
    //!    execution guarantees the new fence's signal happens-after the old
    //!    fence's signal on this queue, so waiting on the new value implies the
    //!    old work has completed.
    //!
    //! Lifetime: m_fence is a non-owning pointer. The owning system (e.g.
    //! AsyncUploadSystem for m_uploadFence) must outlive any resource that
    //! carries a PendingSync referencing its fence.
    struct PendingSync
    {
        Fence*   m_fence      = nullptr;
        uint64_t m_fenceValue = 0;
    };

    // Marks an entity as a shader resource binding.
    struct ShaderResourceTag {};

    // View-to-resource and resource-to-views linked lists.
    struct ViewHierarchy
    {
        RHIHandle m_resource  {NullHandle};
        RHIHandle m_prevView  {NullHandle};
        RHIHandle m_nextView  {NullHandle};
    };

    struct ResourceHierarchy
    {
        RHIHandle m_firstView {NullHandle};
    };

    //////////////////////////////////////////////////////////////
    // Resource initialization — descriptor + placement for buffer creation.
    // RHIResourceSystem consumes this on materialization and removes the component.
    struct PendingBufferInit
    {
        BufferDescriptor m_descriptor;
        HeapMemoryLevel  m_heapMemoryLevel  = HeapMemoryLevel::Device;
        HostMemoryAccess m_hostMemoryAccess = HostMemoryAccess::Write;
    };

    // Resource initialization — descriptor + placement for image creation.
    // RHIResourceSystem consumes this on materialization and removes the component.
    struct PendingImageInit
    {
        ImageDescriptor  m_descriptor;
        HeapMemoryLevel  m_heapMemoryLevel  = HeapMemoryLevel::Device;
        HostMemoryAccess m_hostMemoryAccess = HostMemoryAccess::Write;
    };

    //////////////////////////////////////////////////////////////
    // Upload pipeline components
    // Buffer state machine: [UploadPendingTag] → [BufferUploadSubmitted] → [done]
    // Image  state machine: [UploadPendingTag] → [ImageUploadSubmitted]  → [done]

    // Discovery tag — entity has staged upload data not yet flushed to GPU.
    struct UploadPendingTag {};

    // CPU source data for a buffer upload. Caller guarantees m_data is valid
    // until BOTH PendingBufferUpload AND BufferUploadSubmitted are removed from the entity.
    struct PendingBufferUpload
    {
        const void* m_data              = nullptr;
        size_t      m_dataSize          = 0;
        uint64_t    m_destinationOffset = 0;
    };

    // CPU source data for an image upload. Caller guarantees m_data is valid
    // until BOTH PendingImageUpload AND ImageUploadSubmitted are removed from the entity.
    struct PendingImageUpload
    {
        const void*      m_data                = nullptr;
        size_t           m_dataSize            = 0;
        ImageSubresource m_subresource {};
        Origin           m_destinationOrigin {};
        Size             m_size {};
        Format           m_sourceFormat        = Format::Unknown;
        uint32_t         m_sourceBytesPerRow   = 0;
        uint32_t         m_sourceBytesPerImage = 0;
    };

    // CPU source data + destination range for a host-buffer write via Map.
    // Processed synchronously by RHIResourceSystem::OnFrameBegin: Map →
    // memcpy → Unmap. Only valid for Host heap buffers; Device heap buffers
    // must use PendingBufferUpload instead.
    // Caller guarantees m_data is valid until this component is removed.
    struct PendingBufferMap
    {
        const void* m_data       = nullptr;
        size_t      m_byteOffset = 0;
        size_t      m_byteCount  = 0;
    };

    //! Marks a Buffer entity whose upload has been submitted to the copy queue
    //! and is pending the cross-queue acquire barrier on graphics queue.
    //!
    //! Lifecycle:
    //!  - Added by AsyncUploadSystem::SubmitBatch with the cross-queue
    //!    acquire barrier already constructed (mirror of the release barrier
    //!    emitted on copy queue).
    //!  - Consumed by the RenderGraph executer: when a pass uses the resource
    //!    AND m_fenceValue <= m_uploadFence->GetCompletedValue(), the executer
    //!    emits m_acquireBarrier on the graphics queue (paired with a fence
    //!    wait) and removes this component. Resource is then "fully published".
    //!
    //! Until removed, downstream consumers MUST treat the resource as not yet
    //! safe to use on graphics queue. Has<BufferUploadSubmitted> is the
    //! one-stop readiness check.
    struct BufferUploadSubmitted
    {
        uint64_t      m_fenceValue   = 0;
        Fence*        m_uploadFence  = nullptr;
        BufferBarrier m_acquireBarrier {};
    };

    //! Image counterpart to BufferUploadSubmitted; same semantics.
    struct ImageUploadSubmitted
    {
        uint64_t      m_fenceValue   = 0;
        Fence*        m_uploadFence  = nullptr;
        ImageBarrier  m_acquireBarrier {};
    };
}

namespace Spark::RHI::Components
{
    // Owning resource components. The Ptr<> owns the RHI object lifetime;
    // RHIResourceSystem creates these from descriptors.
    struct Buffer
    {
        Ptr<RHI::Buffer> m_buffer;
    };

    struct Image
    {
        Ptr<RHI::Image> m_image;
    };

    struct BufferView
    {
        Ptr<RHI::BufferView> m_view;
    };

    struct ImageView
    {
        Ptr<RHI::ImageView> m_view;
    };

    struct ShaderResource
    {
        Ptr<RHI::ShaderResource> m_shaderResource;
    };

    // Logical schema of a shader resource bindings layout.
    // Always present on SRG entities, including layout-only ones.
    struct ShaderResourceLayout
    {
        Ptr<RHI::ShaderResourceLayout> m_layout;
    };

    template <typename T>
    using FrameArray = eastl::array<T, RHI::Limits::Device::FrameCountMax>;

    // Per-frame (frame-in-flight) owning variants. The importer adds an empty
    // PerFrame component alongside the descriptor; RHIResourceSystem fills all
    // FrameCountMax slots at materialization time.
    struct ImagePerFrame
    {
        FrameArray<Ptr<RHI::Image>> m_images {};
    };

    struct BufferPerFrame
    {
        FrameArray<Ptr<RHI::Buffer>> m_buffers {};
    };

    struct ImageViewPerFrame
    {
        FrameArray<Ptr<RHI::ImageView>> m_views {};
    };

    struct BufferViewPerFrame
    {
        FrameArray<Ptr<RHI::BufferView>> m_views {};
    };
}
