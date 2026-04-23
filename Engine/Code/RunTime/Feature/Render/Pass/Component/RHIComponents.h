#pragma once

#include <EASTL/type_traits.h>

#include <Object/ObjectName.h>

#include <Pass/RHIHandle.h>

#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>

#include <RHI/Attachment/AttachmentEnums.h>

namespace Spark::Render
{
    template <typename T>
    using FrameArray = eastl::array<T, RHI::Limits::Device::FrameCountMax>;

    struct ResourceName
    {
        ObjectName m_name {};
    };

    struct SwapChainView
    {
        FrameArray<Ptr<RHI::ImageView>> imageViews;
    };

    struct ImportedTag {};

    struct TransientTag {};

    struct PassAttachment
    {
        RHI::AttachmentId m_attachmentId;
        RHI::InputName m_slotName;
        RHI::AttachmentType m_type = RHI::AttachmentType::Uninitialized;
        RHI::AttachmentAccess m_access = RHI::AttachmentAccess::Unknown;
        RHI::AttachmentUsage m_usage = RHI::AttachmentUsage::Uninitialized;
        RHI::AttachmentStage m_stage = RHI::AttachmentStage::Any;
        RHI::AttachmentLoadStoreAction m_action {};
        RHIHandle m_resource;
        union
        {
           RHI::ImageViewDescriptor  m_imageViewDesc {};
           RHI::BufferViewDescriptor m_bufferViewDesc;
        };
        
    };

    static_assert(eastl::is_trivially_copyable_v<RHI::ImageViewDescriptor>);
    static_assert(eastl::is_trivially_copyable_v<RHI::BufferViewDescriptor>);
    static_assert(eastl::is_trivially_copyable_v<PassAttachment>);
    static_assert(eastl::is_default_constructible_v<PassAttachment>);


    struct ResourceHierarchy
    {
        RHIHandle m_firstView;
    };

    struct ViewHierarchy
    {
        RHIHandle m_parentResource;
        RHIHandle m_prevView;
        RHIHandle m_nextView;
    };
}