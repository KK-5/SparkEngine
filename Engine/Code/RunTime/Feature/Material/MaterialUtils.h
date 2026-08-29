#pragma once

#include "Components.h"
#include "MaterialContext.h"

namespace Spark::Material
{
    inline MaterialHandle CreateMaterial(MaterialContext& mc, const Resource::StandardPBR& params)
    {
        MaterialHandle h = mc.CreateEntity();
        mc.Add<Resource::StandardPBR>(h, params);
        return h;
    }

    //! The resident default material (marked by DefaultMaterialTag), or NullMaterial
    //! if none is registered yet. The fallback for unset/dangling references.
    inline MaterialHandle GetDefaultMaterial(MaterialContext& mc)
    {
        return mc.GetView<DefaultMaterialTag>().front();
    }
}
