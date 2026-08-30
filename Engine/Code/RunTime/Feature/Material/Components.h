#pragma once

#include <cstdint>

#include <EASTL/array.h>

#include <Math/Vector3.h>
#include <Math/Vector4.h>
#include <ECS/ComponentTraits.h>
#include <RHI/Context/RHIHandle.h>
#include <Resource/Material/StandardPBR.h>

#include "MaterialHandle.h"

namespace Spark::Material
{
    struct MaterialComponent
    {
        MaterialHandle m_material{NullMaterial};
    };

    //! The material asset a material entity was instantiated from. Resolve() keys on it
    //! to keep one entity per asset; the resident default material carries none.
    struct MaterialAssetRef
    {
        Resource::AssetId m_id;
    };

    //! One object's own parameters, shadowing the material it references. Derives rather
    //! than wraps so it needs no accessors of its own; the distinct type is what lets it
    //! bind to the World while StandardPBR stays bound to the MaterialContext.
    //!
    //! Shadowing is total: once present, the referenced material's parameters no longer
    //! reach this object at all.
    struct StandardPBROverride : Resource::StandardPBR
    {
    };

    //! Resolved per-channel GPU textures (RHIHandles) on the material entity, one per
    //! MaterialTexSlot. Written by MaterialTextureSystem (owned by MaterialSystem), read
    //! by render's MaterialBindingSystem to resolve each bindless index. NullHandle in a
    //! slot = unresolved / no map. This is the producer→consumer handoff component, so it
    //! lives in SparkMaterial (the producer's module); render depends on it and includes it.
    struct MaterialGPUTextures
    {
        eastl::array<RHI::RHIHandle, Resource::MaterialTexSlotCount> m_handles;

        MaterialGPUTextures()
        {
            m_handles.fill(RHI::NullHandle);
        }
    };

    //! Marks the one resident default material in the MaterialContext. Lets any
    //! consumer discover it by querying the context (data-driven, no factory API) —
    //! the fallback target for unset/dangling MaterialComponent references (§1.5).
    struct DefaultMaterialTag {};
}

namespace Spark
{
    // Editable so it appears in the inspector's add-component list.
    SPARK_COMPONENT_TRAITS(Material::MaterialComponent,
        static constexpr bool editable = true;
    )

    // Deliberately NOT editable: an override means nothing without a material to shadow,
    // so it is created from the material slot's own button, never from the generic list.
    SPARK_COMPONENT_TRAITS(Material::StandardPBROverride)
}
