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

    // Marks an SRG entity whose constants / views have been updated and need
    // recompilation this frame. Consumed by RenderGraphCompiler::CompileShaderResources.
    struct ShaderResourceUpdateTag {};

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
    // Buffer state machine: [UploadPendingTag] → [PendingSync] → [done]
    // Image  state machine: [UploadPendingTag] → [PendingSync] → [done]

    // Discovery tag — entity has staged upload data not yet flushed to GPU.
    struct UploadPendingTag {};

    // CPU source data for a buffer upload. Caller guarantees m_data is valid
    // until PendingBufferUpload is removed from the entity.
    //
    // NOTE: re-upload safety uses a CPU-side skip — if the target carries a
    // PendingSync whose fence hasn't reached its value yet, AsyncUploadSystem
    // silently leaves the entity for the next frame instead of submitting now.
    // The entity stays in (UploadPendingTag + PendingBufferUpload) state across
    // these retries, so m_data must remain valid until the upload is actually
    // submitted — potentially several frames after the caller added the
    // component. For first-upload (no prior PendingSync) this lifetime extension
    // is a no-op.
    struct PendingBufferUpload
    {
        const void* m_data              = nullptr;
        size_t      m_dataSize          = 0;
        uint64_t    m_destinationOffset = 0;
    };

    // CPU source data for an image upload. Same m_data lifetime contract as
    // PendingBufferUpload (including the CPU-side skip retry behavior).
    // m_range specifies the subresource range to upload; per-subresource
    // layout (row pitch, image size, dimensions) is queried from the target
    // Image at execution time via GetSubresourceLayouts.
    struct PendingImageUpload
    {
        const void*           m_data              = nullptr;
        size_t                m_dataSize          = 0;
        ImageSubresourceRange m_range {};
        Origin                m_destinationOrigin {};
        Format                m_sourceFormat      = Format::Unknown;
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
