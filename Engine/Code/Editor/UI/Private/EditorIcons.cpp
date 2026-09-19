#include "EditorIcons.h"

#include <Resource/Asset.h>
#include <Service/Service.h>
#include <Feature/UI/ImGui/IconManagerInterface.h>

namespace Editor::Icons
{
    namespace
    {
        constexpr int kCount = static_cast<int>(Icon::Count);

        //! In enum order. Adding an icon without a path here is a compile error rather than a
        //! blank square at runtime.
        constexpr const char* kPaths[kCount] = {
            "editor://APP-Icon.svg",
            "editor://folder.svg",
            "editor://folder-open.svg",
            "editor://Image-icon.svg",
            "editor://mesh.svg",
            "editor://material.svg",
            "editor://new-scene.svg",
            "editor://open-scene.svg",
            "editor://save.svg",
            "editor://exit.svg",
            "editor://Console.svg",
            "editor://Assets.svg",
            "editor://search.svg",
            "editor://unload.svg",
            "editor://loading.svg",
            "editor://edit.svg",
            "editor://override.svg",
            "editor://revert.svg",
            "editor://x-square.svg",
        };

        Spark::Resource::AssetId s_ids[kCount];

        //! OpenIcon creates a world entity and a GPU image on every call, so an id is opened
        //! once and kept. An INVALID one is deliberately not kept: a part can draw before the
        //! icon manager is up, and that must not be remembered as "this icon does not exist".
        const Spark::Resource::AssetId& Resolve(Icon icon)
        {
            const int index = static_cast<int>(icon);

            Spark::Resource::AssetId& id = s_ids[index];
            if (!id.IsValid())
            {
                if (auto* manager = Spark::Service<Spark::UI::IconManagerInterface>::Get())
                {
                    id = manager->OpenIcon(kPaths[index]);
                }
            }
            return id;
        }
    }

    void Load()
    {
        for (int i = 0; i < kCount; ++i)
        {
            Resolve(static_cast<Icon>(i));
        }
    }

    ImTextureID Get(Icon icon)
    {
        auto* manager = Spark::Service<Spark::UI::IconManagerInterface>::Get();
        if (!manager)
        {
            return ImTextureID_Invalid;
        }

        const Spark::Resource::AssetId& id = Resolve(icon);
        if (!id.IsValid())
        {
            return ImTextureID_Invalid;
        }
        return manager->RequestIconId(id);
    }
}
