#pragma once

#include <Object/ObjectName.h>

#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>

namespace Spark::Render
{
    struct ResourceName
    {
        ObjectName m_name {};
    };

    struct RHIBuffer
    {
        RHI::BufferDescriptor m_desc {};
    };

    struct RHIImage
    {
        RHI::ImageDescriptor m_desc {};
    };

    struct RHISampler
    {
        RHI::SamplerState m_desc {};
    };

    struct RHIBufferView
    {
        RHI::BufferViewDescriptor m_desc {};
    };

    struct RHIImageView
    {
        RHI::ImageViewDescriptor m_desc {};
    };


    struct ResourceViewHierichy
    {
        uint32_t m_firstView;
    };

    struct ViewHierichy
    {
        uint32_t m_parentResource;
        uint32_t m_prevView;
        uint32_t m_nextView;
    };
}