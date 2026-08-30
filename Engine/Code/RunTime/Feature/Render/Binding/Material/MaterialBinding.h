#pragma once

#include <Material/MaterialHandle.h>

#include <Binding/GlobalBuffer.h>

#include "MaterialData.h"

namespace Spark::Render
{
    //! On a WORLD entity carrying a StandardPBROverride: the material entity synthesized
    //! for it, which is what actually owns a g_Materials slot. An override is an upper-layer
    //! idea; by the time the GPU sees it there are only materials.
    //!
    //! Deliberately unreflected — the inspector and scene serialization both walk reflected
    //! types, so not registering it is what keeps this machinery out of both.
    struct MaterialOverrideRef
    {
        Material::MaterialHandle m_material{Material::NullMaterial};
    };

    //! World overrides -> their synthesized material entities, and the reverse for links
    //! whose override is gone. Free functions because neither needs anything but the two
    //! execute contexts; MaterialBindingSystem only decides when they run.
    void SyncOverrideMaterials();

    //! Destroys every material entity wearing DeadTag. Must run AFTER the encode:
    //! GlobalBuffer reclaims a slot by seeing the entity carry both its slot ref and the
    //! tag, so the entity has to survive one pass wearing it.
    void ReapDeadMaterials();

    //! Names the g_Materials array (space3) for GlobalBuffer and its slot refs.
    struct Materials {};

    //! A material's reference to its g_Materials slot, on the MATERIAL entity. m_id is
    //! the GPU index directly and does not move for the material's life, so a consumer
    //! resolves it once per encode instead of reading a slot rewritten every frame.
    //! IsValid() is what tells a stale copy apart from a live one.
    using MaterialSlotRef = GlobalBufferSlotRef<Materials>;

    //! Marks the single ShaderBindings entity (in the RHIContext) that holds the
    //! g_Materials SRV at space3. A pass declares .Binds<MaterialBindingTag>() and the
    //! executer binds it once before the pass's draws, exactly like MainViewTag (space1).
    struct MaterialBindingTag {};
}
