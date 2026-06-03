#pragma once

#include <ECS/ISystem.h>
#include <ECS/SystemTraits.h>
#include <ECS/Common.h>
#include <Service/Service.h>

#include "IconManagerInterface.h"
#include "Components.h"

namespace Spark::UI
{
    class IconManager final : public ISystem,
                              public Service<IconManagerInterface>::Handler
    {
    public:
        SPARK_COMPONENT_ACCESS(
            ReadWriteComponent<IconComponent>,
            WriteComponent<IconGPUComponent>
        );

        SPARK_SYSTEM_TRAITS(IconManager);

        // ISystem
        void InitInternal() override {}
        void ShutdownInternal() override;
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override { return "IconManager"; }

        // IconManagerInterface
        Resource::AssetId OpenIcon(eastl::string_view imagePath) override;
        ImTextureID       RequestIconId(Resource::AssetId id) override;
    };
}