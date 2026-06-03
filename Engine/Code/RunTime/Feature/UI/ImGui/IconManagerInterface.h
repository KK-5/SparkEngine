#pragma once

#include <EASTL/string_view.h>
#include <Resource/Asset.h>

#include <imgui.h>


namespace Spark::UI
{
    class IconManagerInterface
    {
    public:
        virtual ~IconManagerInterface() = default;
        virtual Resource::AssetId OpenIcon(eastl::string_view imagePath) = 0;

        virtual ImTextureID RequestIconId(Resource::AssetId id) = 0;

    };
}