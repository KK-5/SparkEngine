#pragma once

#include <imgui.h>

#include <Resource/Asset.h>
#include <Resource/Image/ImageAsset.h>

#include <RHI/Context/UniqueRHIHandle.h>
#include <RHI/Resource/AccessFlags.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>

namespace Spark::UI
{
    struct IconComponent
    {
        Resource::AssetId         m_iconAssetId;
        Ptr<Resource::ImageAsset> m_iconImageAsset;
    };

    //! Owns the icon's image: the component going lets it go.
    struct IconGPUComponent
    {
        RHI::UniqueRHIHandle     m_image;
        RHI::ImageViewDescriptor m_viewDesc {};
        ImTextureID              m_iconId   {ImTextureID_Invalid};
    };

    //! On the RHIContext entity of each icon image (IconGPUComponent::m_image), which the UI
    //! samples.
    struct IconImageTag
    {
        static constexpr RHI::AccessFlags Access = RHI::AccessFlags::ShaderSampledRead;
    };
}