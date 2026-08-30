#pragma once

#include <Resource/Material/MaterialState.h>

#include "Components.h"
#include "MaterialContext.h"

namespace Spark::Material
{
    inline MaterialHandle CreateMaterial(MaterialContext& mc,
                                         const Resource::StandardPBR& params,
                                         const Resource::MaterialState& state = {})
    {
        MaterialHandle h = mc.CreateEntity();
        mc.Add<Resource::StandardPBR>(h, params);
        mc.Add<Resource::MaterialState>(h, state);
        return h;
    }

    //! The material entity for `id`, created on first use and shared by every later
    //! reference to the same asset. NullMaterial while the asset is not readable yet —
    //! callers fall back to the default material for that frame.
    MaterialHandle Resolve(MaterialContext& mc, const Resource::AssetId& id);

    //! The resident default material (marked by DefaultMaterialTag), or NullMaterial
    //! if none is registered yet. The fallback for unset/dangling references.
    inline MaterialHandle GetDefaultMaterial(MaterialContext& mc)
    {
        return mc.GetView<DefaultMaterialTag>().front();
    }
}
