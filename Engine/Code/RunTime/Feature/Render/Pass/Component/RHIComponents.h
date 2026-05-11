#pragma once

#include <EASTL/type_traits.h>

#include <Object/ObjectName.h>
#include <EASTLEX/hash.h>

#include <RHI/Context/RHIContext.h>
#include <Pass/Pass.h>

#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>
#include <RHI/Resource/ShaderResource/ShaderResource.h>
#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <RHI/Resource/ResourceState.h>

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>

namespace Spark::Render
{
    //! RHIContext lives in the RHI layer (Spark::RHI). Re-expose its handle,
    //! null sentinel, and ECS-context aliases unqualified so Render-namespace
    //! component types, builders, compilers and pass code keep their usage.
    using RHI::RHIHandle;
    using RHI::NullHandle;
    using RHI::RHIContext;
    using RHI::RHIExecuteContext;
    using RHI::RHIExecuteContextGuard;

    ///////////////////////////////////////////////
    // Resource(Buffer, Image, BufferView, ImageView) component
    template <typename T>
    using FrameArray = eastl::array<T, RHI::Limits::Device::FrameCountMax>;

    struct ResourceName
    {
        ObjectName m_name {};
    };

    struct SwapChainViews
    {
        FrameArray<Ptr<RHI::ImageView>> imageViews;
    };

    struct SwapChainImages
    {
        FrameArray<RHI::Image*> images;
    };

    struct ImportedTag {};

    struct TransientTag {};

    //! Marks an RHI resource entity whose CPU-side staging state has been mutated
    //! and needs flushing this frame. Generic across resource types — currently
    //! consumed by the ShaderResource batch Compile pass; can extend to other
    //! resource updates without introducing per-resource dirty tags. Cleared
    //! after the consumer walks the view.
    struct RHIUpdateTag {};


    //! Owning resource components for imported resources. Lifetime managed by
    //! the external importer (asset system, per-frame buffer pool, ...). Render
    //! graph never touches these — it reads BackingImage/BackingBuffer below,
    //! which the importer refreshes from these on a schedule it controls
    //! (single-frame: once at registration; per-frame: every OnFrameBegin).
    struct Image
    {
        Ptr<RHI::Image> m_image;
    };

    struct Buffer
    {
        Ptr<RHI::Buffer> m_buffer;
    };

    //! Per-frame (frame-in-flight) owning variants. Used for swap chain images,
    //! per-frame UBO / staging buffers, query result buffers, etc. The importer
    //! fills all FrameCountMax slots once at registration; per-frame refresh of
    //! BackingImage/BackingBuffer picks the active slot via the current frameIndex.
    struct ImagePerFrame
    {
        FrameArray<Ptr<RHI::Image>> m_images {};
    };

    struct BufferPerFrame
    {
        FrameArray<Ptr<RHI::Buffer>> m_buffers {};
    };

    //! Non-owning pointer to the actual RHI resource backing a resource entity.
    //! Render graph (barrier compile, attachment resolution, executer) reads only
    //! this component — imported and transient resources are unified here.
    //!  - Transient: written by RenderGraphCompiler::CompileTransientResources
    //!    after the pool allocates.
    //!  - Imported single-frame: written once by the importer at registration,
    //!    points into the entity's own Image::m_image.
    //!  - Imported per-frame: written each OnFrameBegin by the importer,
    //!    points into ImagePerFrame::m_images[frameIndex].
    //! Lifetime is managed externally — transient pool or importing owner.
    struct BackingImage
    {
        RHI::Image* m_image = nullptr;
    };

    struct BackingBuffer
    {
        RHI::Buffer* m_buffer = nullptr;
    };

    //! Owning view components on a view entity. Mirror of Image / Buffer on
    //! the resource side — caller (transient view materialization, importer)
    //! owns the underlying RHI view via Ptr<>; destroying the view entity
    //! releases it. The transient/imported distinction is expressed by
    //! TransientTag / ImportedTag stamped on the view entity itself, not by
    //! the component type.
    struct ImageView
    {
        Ptr<RHI::ImageView> m_view;
    };

    struct BufferView
    {
        Ptr<RHI::BufferView> m_view;
    };

    //! Per-frame (frame-in-flight) owning variants. Used when the underlying
    //! view rotates with the resource it views (e.g. swap chain image views).
    struct ImageViewPerFrame
    {
        FrameArray<Ptr<RHI::ImageView>> m_views {};
    };

    struct BufferViewPerFrame
    {
        FrameArray<Ptr<RHI::BufferView>> m_views {};
    };

    //! Non-owning pointer to the actual RHI view backing a view entity.
    //! Render graph (CompileRenderPassBeginInfo, attachment binding, etc.) reads
    //! only this component — transient and imported, single-frame and per-frame
    //! views are unified here.
    //!  - Transient: written by RenderGraphCompiler at view materialization,
    //!    points into the entity's own ImageView::m_view.
    //!  - Imported single-frame: written once by the importer at registration,
    //!    points into ImageView::m_view.
    //!  - Imported per-frame: written each OnFrameBegin by the importer,
    //!    points into ImageViewPerFrame::m_views[frameIndex].
    //! Lifetime is managed by the owning Ptr<> on the same entity.
    struct BackingImageView
    {
        RHI::ImageView* m_view = nullptr;
    };

    struct BackingBufferView
    {
        RHI::BufferView* m_view = nullptr;
    };

    //! Discovery tag — marks an entity as a shader resource binding. Useful for
    //! debug views ("list all SRGs") and component-presence-based identification
    //! when a generic RHI view doesn't carry enough information.
    struct ShaderResourceTag {};

    //! Owning shader resource — the descriptor table / root signature binding
    //! container the shader actually samples from. Mirrors Image / Buffer:
    //! the Ptr<> is the lifetime owner; consumers read BackingShaderResource.
    //! Only present on concrete instances; layout-only entities (per-draw
    //! layout slots) intentionally omit this and BackingShaderResource.
    struct ShaderResource
    {
        Ptr<RHI::ShaderResource> m_shaderResource;
    };

    //! Non-owning pointer to the actual RHI shader resource. Read by the
    //! executer to bind at pass begin (concrete slot) or by execute lambdas
    //! to bind dynamically per draw. Absence of this component on an SRG
    //! entity is the per-draw / layout-only signal — no extra tag needed.
    struct BackingShaderResource
    {
        RHI::ShaderResource* m_shaderResource = nullptr;
    };

    //! Logical schema of a shader resource — what bindings exist, in what
    //! slots. Always present on SRG entities, including layout-only ones.
    //! Read by the PSO compiler to assemble PipelineLayoutDescriptor.
    struct ShaderResourceLayout
    {
        Ptr<RHI::ShaderResourceLayout> m_layout;
    };

    struct ImportedResourceState
    {
        RHI::ResourceState      m_initial;
        RHI::AttachmentStage    m_initialStage = RHI::AttachmentStage::Any;
        RHI::HardwareQueueClass m_initialQueue = RHI::HardwareQueueClass::Graphics;
        RHI::ResourceState      m_final;
        RHI::AttachmentStage    m_finalStage = RHI::AttachmentStage::Any;
        RHI::HardwareQueueClass m_finalQueue = RHI::HardwareQueueClass::Graphics;
    };

    // Simulated resource state during barrier compile.
    // Lives on Resource entities; seeded lazily on first touch, cleared at end of frame.
    struct ResourceStateTracker
    {
        RHI::ResourceState   m_current {};
        Pass                 m_lastPass  { NullPass };
        RHI::AttachmentStage m_lastStage { RHI::AttachmentStage::Any };
    };

    struct ResourceHierarchy
    {
        RHIHandle m_firstView {NullHandle};
    };

    struct ViewHierarchy
    {
        RHIHandle m_resource {NullHandle};
        RHIHandle m_prevView {NullHandle};
        RHIHandle m_nextView {NullHandle};
    };
    ///////////////////////////////////////////////

    ///////////////////////////////////////////////
    // Attachment component
    struct AttachmentId
    {
        RHI::AttachmentId m_id;
        uint32_t m_version {0};

        bool operator==(const AttachmentId& other) const
        {
            return m_id == other.m_id && m_version == other.m_version;
        }

        bool operator!=(const AttachmentId& other) const
        {
            return !(*this == other);
        }

        AttachmentId Next() const
        {
            return AttachmentId{m_id, m_version + 1};
        }

        bool IsValid() const
        {
            return !m_id.IsEmpty();
        }
    };


    struct ImagePassAttachment
    {
        AttachmentId                   m_attachmentId;
        RHI::InputName                 m_slotName;
        RHI::AttachmentAccess          m_access = RHI::AttachmentAccess::Unknown;
        RHI::AttachmentUsage           m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage           m_stage  = RHI::AttachmentStage::Any;
        RHI::AttachmentLoadStoreAction m_action {};
        //! View descriptor for transient attachments (m_view is NullHandle until
        //! CompileTransientResources materializes the view). Ignored for imported
        //! attachments — their m_view is already a fully formed view entity whose
        //! descriptor lives on the view itself.
        RHI::ImageViewDescriptor       m_viewDescriptor {};
        RHIHandle                      m_view {NullHandle};
        Pass                           m_pass {NullPass};
    };

    struct BufferPassAttachment
    {
        AttachmentId              m_attachmentId;
        RHI::InputName            m_slotName;
        RHI::AttachmentAccess     m_access = RHI::AttachmentAccess::Unknown;
        RHI::AttachmentUsage      m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage      m_stage  = RHI::AttachmentStage::Any;
        //! See ImagePassAttachment::m_viewDescriptor.
        RHI::BufferViewDescriptor m_viewDescriptor {};
        RHIHandle                 m_view {NullHandle};
        Pass                      m_pass {NullPass};
    };

    static_assert(eastl::is_trivially_copyable_v<ImagePassAttachment>);
    static_assert(eastl::is_default_constructible_v<ImagePassAttachment>);
    static_assert(eastl::is_trivially_copyable_v<BufferPassAttachment>);
    static_assert(eastl::is_default_constructible_v<BufferPassAttachment>);

    struct AttachmentCompilingTag {};
    /////////////////////////////////////////////////
}

namespace eastl
{
    template<>
    struct hash<Spark::Render::AttachmentId>
    {
        size_t operator()(const Spark::Render::AttachmentId& id) const noexcept
        {
            size_t h = hash<Spark::ObjectName>{}(id.m_id);
            hash_combine(h, id.m_version);
            return h;
        }
    };
}