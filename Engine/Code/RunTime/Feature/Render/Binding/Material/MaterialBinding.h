#pragma once

#include <Material/Components.h>
#include <Material/MaterialHandle.h>

#include <Pass/TagList.h>
#include <Suballocation/Suballocator.h>

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

    //! Destroys every material entity wearing DeadTag.
    void ReapDeadMaterials();

    //! A material's g_Materials slot, on the MATERIAL entity: a MaterialData-sized range of the
    //! array's address space. Get() is the GPU index directly and does not move for the
    //! material's life, so a consumer resolves it once per encode instead of reading a slot
    //! rewritten every frame.
    struct MaterialSlot
    {
        Suballocation m_range;

        uint32_t Get() const { return static_cast<uint32_t>(m_range.GetOffset() / sizeof(MaterialData)); }
    };

    //! Marks the single ShaderBindings entity (in the RHIContext) that holds the
    //! g_Materials SRV at space3. A pass declares .Binds<MaterialBindingTag>() and the
    //! executer binds it once before the pass's draws, exactly like ViewBindingTag (space1).
    struct MaterialBindingTag
    {
        using Uses = TagList<Material::MaterialTextureTag>;
    };

    //! The buffer entity g_Materials is a view of, written in the graph. A pass whose shaders
    //! read g_Materials imports it by this tag and declares the read in each Scope.
    struct MaterialPoolTag {};
}
