#pragma once

#include <Object/ObjectName.h>

#include <Pass/RHIHandle.h>

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

    struct SwapChainView
    {
        uint32_t m_index;
    };
    /*
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
    */

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