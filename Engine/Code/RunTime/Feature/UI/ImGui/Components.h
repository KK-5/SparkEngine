#pragma once

#include <imgui.h>

#include <Resource/Asset.h>
#include <Resource/Image/ImageAsset.h>

#include <RHI/Context/RHIHandle.h>
#include <RHI/Resource/Image/ImageView.h>

namespace Spark::UI
{
    struct IconComponent
    {
        Resource::AssetId         m_iconAssetId;
        Ptr<Resource::ImageAsset> m_iconImageAsset;
    };

    struct IconGPUComponent
    {
        RHI::RHIHandle  m_image     {RHI::NullHandle};  
        RHI::RHIHandle  m_imageView {RHI::NullHandle};
        ImTextureID     m_iconId    {ImTextureID_Invalid};
    };
}