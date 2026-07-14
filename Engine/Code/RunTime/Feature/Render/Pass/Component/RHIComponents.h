#pragma once

#include <EASTL/type_traits.h>

#include <Object/ObjectName.h>
#include <EASTLEX/hash.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <Pass/Pass.h>

#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/ShaderInput/ShaderInputDescriptor.h>
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
    // RHI-layer components re-exported into Render namespace.
    // Code that already uses Spark::Render::Xxx continues to compile.
    using RHI::ImportedTag;
    using RHI::StaticImportTag;
    using RHI::TransientTag;
    using RHI::RHIUpdateTag;
    using RHI::ShaderBindingsUpdateTag;
    using RHI::ResourceName;

    using RHI::Components::Buffer;
    using RHI::Components::Image;
    using RHI::Components::FrameArray;
    using RHI::Components::ImagePerFrame;
    using RHI::Components::BufferPerFrame;
    ///////////////////////////////////////////////

    ///////////////////////////////////////////////
    // SwapChain-specific component (Render-layer concept). Holds the back-buffer
    // images borrowed from the SwapChain (raw pointers — the swap chain owns them).
    // Views are NOT stored here: they live in the resource's ImageViewCachePerFrame,
    // built lazily per frame via GetOrCreateImageViewPerFrame.
    struct SwapChainImages
    {
        FrameArray<RHI::Image*> images;
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

    // Simulated resource state during barrier compile.
    // Lives on Resource entities; seeded lazily on first touch, cleared at end of frame.
    // m_current is the full post-barrier state (usage + access + queue + stage) — read
    // it for both srcQueue and srcStage when constructing the next barrier.
    // m_lastPass is kept only to know which pass entity to push the cross-queue release
    // (m_postBuffer / m_postImage) onto; NullPass means first touch (no producer yet).
    struct ResourceStateTracker
    {
        RHI::ResourceState m_current {};
        Pass               m_lastPass { NullPass };
    };

    // Per-attachment compiled barrier: CompileImage/BufferBarriers emit one of these
    // onto the Attachment entity, then the per-pass merge in CompileResourceBarriers
    // folds all attachments referencing the same resource into a single barrier
    // (combined-read AccessFlags OR). Lives on the Attachment (not the resource) so the
    // merge recovers the resource entity via ImagePassAttachment::m_image /
    // BufferPassAttachment::m_buffer. Per-pass lifetime: cleared once the merge consumes it.
    struct CompiledImageBarrier
    {
        RHI::ImageBarrier m_barrier;
    };

    struct CompiledBufferBarrier
    {
        RHI::BufferBarrier m_barrier;
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
        RHI::InputName                 m_resolveSourceSlot;
        RHI::AttachmentAccess          m_access = RHI::AttachmentAccess::Unknown;
        RHI::AttachmentUsage           m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage           m_stage  = RHI::AttachmentStage::Any;
        RHI::AttachmentLoadStoreAction m_action {};  // Only meaningful for RenderTarget / DepthStencil; ignored for Shader, Copy, etc.
        //! View descriptor keying this attachment's view in the resource's view
        //! cache. The view is resolved on demand via GetOrCreateImageView(m_image,
        //! m_viewDescriptor) — deduplicated and reused per resource.
        RHI::ImageViewDescriptor       m_viewDescriptor {};
        //! The image resource entity this attachment uses (its primary link).
        //! Filled at Build for Create/Import and at compile (lifetime sweep) for
        //! Read/Write. Views are resolved from this resource's cache, not stored
        //! per attachment.
        RHIHandle                      m_image {NullHandle};
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
        //! The buffer resource entity this attachment uses (its primary link).
        //! Mirrors ImagePassAttachment::m_image. Filled at Build for Create/Import
        //! and at compile (lifetime sweep) for Read/Write.
        RHIHandle                 m_buffer {NullHandle};
        Pass                      m_pass {NullPass};
    };

    // ObjectName (the underlying type of RHI::AttachmentId) embeds an eastl::string
    // in debug builds for log/inspection, which makes AttachmentId — and therefore
    // these components — non-trivially-copyable. Only enforce the POD invariant in
    // builds where ObjectName is stripped to its 8-byte hash. TODO: move the debug
    // string to a global intern table so the invariant holds in all configs.
#if !SPARK_OBJECT_NAME_KEEP_STRING
    static_assert(eastl::is_trivially_copyable_v<ImagePassAttachment>);
    static_assert(eastl::is_trivially_copyable_v<BufferPassAttachment>);
#endif
    static_assert(eastl::is_default_constructible_v<ImagePassAttachment>);
    static_assert(eastl::is_default_constructible_v<BufferPassAttachment>);

    struct AttachmentCompilingTag {};

    //! Runtime marker on every per-pass ShaderBindings entity created via
    //! GetOrCreatePassShaderBindings. Lets teardown reap them all with a single
    //! GetView<PassShaderBindingsTag>, independent of the compile-time PassTag they
    //! also carry. Per-pass SRGs are persistent (built once, reused each frame) and
    //! have no external owner, so this tag is their collection handle.
    struct PassShaderBindingsTag {};
    /////////////////////////////////////////////////

    ///////////////////////////////////////////////
    // Static-import resource attachment helpers
    //
    // Call once at resource creation time (not in the Build lambda) to store
    // BufferPassAttachment / ImagePassAttachment directly on the static resource
    // entity (m_view == NullHandle).  The component persists across frames and is
    // consumed each compile by CompileStaticResourceBarriers, which drives the
    // CopyDst → target-usage barrier without any per-pass Build registration.
    //
    // Pattern:
    //   auto vb = RHI::CreateStaticBuffer(ctx, name, desc);
    //   Render::CreateStaticBufferAttachment(ctx, vb,
    //       RHI::InputName("MyVB"),
    //       RHI::AttachmentAccess::Read,
    //       RHI::AttachmentUsage::InputAssembly,
    //       RHI::AttachmentStage::VertexInput);
    ///////////////////////////////////////////////

    inline void CreateStaticBufferAttachment(
        RHIContext&              ctx,
        RHIHandle                resourceEntity,
        RHI::InputName           slot,
        RHI::AttachmentAccess    access,
        RHI::AttachmentUsage     usage,
        RHI::AttachmentStage     stage)
    {
        BufferPassAttachment a;
        a.m_attachmentId = AttachmentId{ ctx.Get<ResourceName>(resourceEntity).m_name, 0 };
        a.m_slotName     = slot;
        a.m_access       = access;
        a.m_usage        = usage;
        a.m_stage        = stage;
        a.m_pass         = NullPass;    // persists across frames, not tied to one pass
        ctx.AddOrReplace<BufferPassAttachment>(resourceEntity, a);
    }

    inline void CreateStaticImageAttachment(
        RHIContext&              ctx,
        RHIHandle                resourceEntity,
        RHI::InputName           slot,
        RHI::AttachmentAccess    access,
        RHI::AttachmentUsage     usage,
        RHI::AttachmentStage     stage)
    {
        ImagePassAttachment a;
        a.m_attachmentId = AttachmentId{ ctx.Get<ResourceName>(resourceEntity).m_name, 0 };
        a.m_slotName     = slot;
        a.m_access       = access;
        a.m_usage        = usage;
        a.m_stage        = stage;
        a.m_pass         = NullPass;
        ctx.AddOrReplace<ImagePassAttachment>(resourceEntity, a);
    }
    ///////////////////////////////////////////////
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
