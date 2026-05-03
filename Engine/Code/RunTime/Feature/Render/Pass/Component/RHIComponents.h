#pragma once

#include <EASTL/type_traits.h>

#include <Object/ObjectName.h>
#include <EASTLEX/hash.h>

#include <Pass/RHIHandle.h>
#include <Pass/Pass.h>

#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>
#include <RHI/Resource/ResourceState.h>

#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>

namespace Spark::Render
{
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


    //! Non-owning pointer to the actual RHI resource backing a resource entity.
    //! For transient resources, populated by RenderGraphCompiler::CompileTransientResources
    //! after the pool allocates. For imported resources, populated by the importer
    //! (e.g. RenderSystem for swapchain, or external code).
    //! Lifetime is managed externally — transient pool or importing owner.
    struct BackingImage
    {
        RHI::Image* m_image = nullptr;
    };

    struct BackingBuffer
    {
        RHI::Buffer* m_buffer = nullptr;
    };



    //! Create RHI::ImageView / RHI::BufferView for a view entity that
    //! wraps a transient resource. The view object is owned by this component
    //! (Ptr<>); destroying the view entity releases it.
    struct TransientImageView
    {
        Ptr<RHI::ImageView> m_view;
    };

    struct TransientBufferView
    {
        Ptr<RHI::BufferView> m_view;
    };

    struct ImportedResourceState
    {
        RHI::ResourceState m_initial;
        RHI::ResourceState m_final;
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