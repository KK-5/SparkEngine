#pragma once

#include <Math/Vector4.h>
#include <ECS/ComponentTraits.h>
#include <Resource/AssetTypes.h>

#include "MaterialHandle.h"

namespace Spark::Material
{
    struct MaterialParams
    {
        Math::Vector4 m_baseColor{0.8f, 0.8f, 0.8f, 1.0f};   // rgb (+a reserved)
        float         m_metallic  = 0.0f;
        float         m_roughness = 0.5f;
        float         m_specular  = 0.5f;   // dielectric F0 scale; 0.5 -> F0 0.04

        Resource::AssetId m_baseColorTexture;
    };

    struct MaterialComponent
    {
        MaterialHandle m_material{NullMaterial};
    };

    //! Marks the one resident default material in the MaterialContext. Lets any
    //! consumer discover it by querying the context (data-driven, no factory API) —
    //! the fallback target for unset/dangling MaterialComponent references (§1.5).
    struct DefaultMaterialTag {};
}

namespace Spark
{
    // Editable so it appears in the inspector's add-component list; no component
    // events needed (the render-side binding reads it via a per-frame view).
    SPARK_COMPONENT_TRAITS(Material::MaterialComponent,
        static constexpr bool editable = true;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::None;
    )
}
