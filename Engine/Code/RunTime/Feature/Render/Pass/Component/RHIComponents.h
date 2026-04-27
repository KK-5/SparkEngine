#pragma once

#include <EASTL/type_traits.h>

#include <Object/ObjectName.h>

#include <Pass/RHIHandle.h>
#include <Pass/Pass.h>

#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>
#include <RHI/Resource/ResourceState.h>

#include <RHI/Attachment/AttachmentEnums.h>

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
    struct ImagePassAttachment
    {
        RHI::AttachmentId              m_attachmentId;
        RHI::InputName                 m_slotName;
        RHI::AttachmentAccess          m_access = RHI::AttachmentAccess::Unknown;
        RHI::AttachmentUsage           m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage           m_stage  = RHI::AttachmentStage::Any;
        RHI::AttachmentLoadStoreAction m_action {};
        RHIHandle                      m_view {NullHandle};
        Pass                           m_pass {NullPass};
    };

    struct BufferPassAttachment
    {
        RHI::AttachmentId     m_attachmentId;
        RHI::InputName        m_slotName;
        RHI::AttachmentAccess m_access = RHI::AttachmentAccess::Unknown;
        RHI::AttachmentUsage  m_usage  = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage  m_stage  = RHI::AttachmentStage::Any;
        RHIHandle             m_view {NullHandle};
        Pass                  m_pass {NullPass};
    };

    static_assert(eastl::is_trivially_copyable_v<ImagePassAttachment>);
    static_assert(eastl::is_default_constructible_v<ImagePassAttachment>);
    static_assert(eastl::is_trivially_copyable_v<BufferPassAttachment>);
    static_assert(eastl::is_default_constructible_v<BufferPassAttachment>);

    struct AttachmentCompilingTag {};
    /////////////////////////////////////////////////
}