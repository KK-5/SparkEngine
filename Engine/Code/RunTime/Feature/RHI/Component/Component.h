#pragma once

#include <EASTL/array.h>

#include <Object/ObjectName.h>

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
    // Upload pipeline components
    // Entity state machine: [UploadPendingTag] → [UploadSubmitted] → [done]

    // Discovery tag — entity has staged upload data not yet flushed to GPU.
    struct UploadPendingTag {};

    // CPU source data for a buffer upload. Caller guarantees m_data is valid
    // until UploadSubmitted is removed (or FlushAndWait returns).
    struct PendingBufferUpload
    {
        const void* m_data              = nullptr;
        size_t      m_dataSize          = 0;
        uint64_t    m_destinationOffset = 0;
    };

    // CPU source data for an image upload. Caller guarantees m_data is valid
    // until UploadSubmitted is removed (or FlushAndWait returns).
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

    // Marks an entity whose upload batch has been submitted to the copy queue
    // but not yet signalled complete. Removed by AsyncUploadSystem on poll.
    struct UploadSubmitted
    {
        uint64_t m_fenceValue = 0;
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
