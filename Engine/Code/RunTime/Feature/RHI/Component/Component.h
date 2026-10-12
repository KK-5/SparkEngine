#pragma once

#include <EASTL/array.h>
#include <EASTL/fixed_vector.h>

#include <Object/ObjectName.h>

#include <RHI/MemoryEnums.h>
#include <RHI/RHILimits.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
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

    // Discovery tags — placed on RESOURCE entities only. Views are not entities;
    // they live in each resource's view cache (see Components::ImageViewCache).
    struct ImportedTag {};
    struct TransientTag {};

    // Resource multiplicity tags — determines single vs. per-frame allocation.
    // Absence of PerFrameTag defaults to single-frame behavior.
    struct PerFrameTag {};

    // Marks an RHI resource entity whose CPU-side staging state has been mutated
    // and needs flushing this frame.
    struct RHIUpdateTag {};

    // Marks a ShaderBindings entity whose constants / views have been updated
    // and need recompilation this frame. Consumed by
    // RenderGraphCompiler::CompileShaderInputs.
    struct ShaderBindingsUpdateTag {};

    // Human-readable debug name on a resource entity.
    struct ResourceName
    {
        ObjectName m_name {};
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
    //
    // The upload requests themselves are entities of RHIEventContext (EventComponent.h).

    // While uploads into the resource are not over: how many. What a reader of the
    // resource asks, with the handle it already has.
    struct PendingUploads
    {
        uint32_t m_count = 0;
    };

    // CPU source data + destination range for a host-buffer write via Map.
    // Processed synchronously by RHIResourceSystem::OnFrameBegin: Map →
    // memcpy → Unmap. Only valid for Host heap buffers; Device heap buffers
    // must use RequestBufferUpload instead.
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

    //! One cached single-frame image view on a resource: its descriptor (cache key)
    //! and the owning RHI view. The view pointer (m_view.get()) is stable for the
    //! resource's lifetime, so consumers may bake it directly into compile products.
    struct ImageViewCacheEntry
    {
        RHI::ImageViewDescriptor m_descriptor {};
        Ptr<RHI::ImageView>      m_view;
    };

    //! Resource-owned, descriptor-keyed image view cache (lives on the RESOURCE
    //! entity). A resource is pure memory; each distinct ImageViewDescriptor on it
    //! is one shared (deduplicated) view. Views live here, NOT as separate entities:
    //! they are lightweight, never shared across owners (only reused), carry only
    //! closed per-view data, and are never queried across entities — i.e. the
    //! resource's intrinsic data, so a bounded container is the right home (not the
    //! "container-in-component" smell). Append is an in-place write (parallel-
    //! friendly per resource) once the empty cache is pre-added at resource creation.
    //!
    //! Multiplicity mirrors the resource: single-frame resources (Image) carry this;
    //! per-frame resources (ImagePerFrame) carry ImageViewCachePerFrame instead.
    struct ImageViewCache
    {
        static constexpr uint32_t MaxViews = 8;
        eastl::fixed_vector<ImageViewCacheEntry, MaxViews> m_entries;
    };

    //! Buffer-view counterpart of ImageViewCacheEntry / ImageViewCache — same
    //! rationale (resource-owned, descriptor-keyed, deduplicated, views not entities).
    struct BufferViewCacheEntry
    {
        RHI::BufferViewDescriptor m_descriptor {};
        Ptr<RHI::BufferView>      m_view;
    };

    struct BufferViewCache
    {
        static constexpr uint32_t MaxViews = 8;
        eastl::fixed_vector<BufferViewCacheEntry, MaxViews> m_entries;
    };

    // Owning component holding a ShaderBindings instance. Placed on an entity by
    // Render::CreatePassBindings so RenderGraphCompiler::CompileShaderInputs
    // can discover it via view iteration. Filter tag is ShaderBindingsUpdateTag.
    struct ShaderBindings
    {
        Ptr<RHI::ShaderBindings> m_bindings;
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

    //! Per-frame counterpart of ImageViewCacheEntry: one descriptor keys N owning
    //! views (one per frame-in-flight). Unlike the single-frame entry there is no
    //! stable pointer to bake at compile time — the concrete view must be resolved
    //! at execute by frameIndex (m_views[frameIndex].get()).
    struct ImageViewCachePerFrameEntry
    {
        RHI::ImageViewDescriptor        m_descriptor {};
        FrameArray<Ptr<RHI::ImageView>> m_views {};
    };

    //! Per-frame view cache on a per-frame resource entity (ImagePerFrame). See
    //! ImageViewCache for the single-frame rationale; this variant differs only in
    //! that each entry owns one view per frame-in-flight and is resolved by frame.
    struct ImageViewCachePerFrame
    {
        static constexpr uint32_t MaxViews = 8;
        eastl::fixed_vector<ImageViewCachePerFrameEntry, MaxViews> m_entries;
    };

    //! Per-frame buffer-view cache — buffer counterpart of ImageViewCachePerFrame.
    struct BufferViewCachePerFrameEntry
    {
        RHI::BufferViewDescriptor        m_descriptor {};
        FrameArray<Ptr<RHI::BufferView>> m_views {};
    };

    struct BufferViewCachePerFrame
    {
        static constexpr uint32_t MaxViews = 8;
        eastl::fixed_vector<BufferViewCachePerFrameEntry, MaxViews> m_entries;
    };
}
