#pragma once

#include <ECS/Bus/EntityEventBus.h>
#include <RHI/Context/RHIContext.h>

#include <Binding/DeviceArray.h>
#include <Material/Components.h>
#include <Material/MaterialContext.h>
#include <Pass/PassContext.h>

#include "MaterialBinding.h"

namespace Spark::Render
{
    //! Owns the g_Materials array (space3) + its ShaderBindings. Every material entity
    //! carrying Resource::StandardPBR gets a stable slot, a MaterialSlot, and its parameters
    //! are encoded into that slot's g_Materials record. g_Materials is a DeviceArray, which
    //! writes the records that changed: a material's, when it is edited or a texture arrives.
    //!
    //! A world entity's StandardPBROverride is synthesized into a material entity of its
    //! own here, so the buffer keeps one population and the shader keeps one array. That
    //! material is not asset-backed and never leaves this system.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked before
    //! InstanceBindingSystem so a material's slot exists before an instance bakes it.
    class MaterialBindingSystem final : public EntityEventBus::Handler
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! Registers the pass writing the records, ahead of every pass that reads them.
        void SetUp(PassContext& passContext);
        void Update();
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

        void OnEntityCreate(Entity) override {}
        //! The one case SyncOverrideMaterials cannot see: the override and the link die
        //! with the entity, leaving nothing that still names the synthesized material.
        void OnEntityDestory(Entity entity) override;

    private:
        //! Fixed upper bound on live materials. Overflow logs and drops the surplus
        //! (rendering is not blocked).
        static constexpr uint32_t Capacity = 1u << 16;

        //! A slot for each material with StandardPBR; one that lost it gives its back.
        void AllocateSlots(Material::MaterialContext& matCtx);

        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Materials @ space3

        //! g_Materials, carrying MaterialPoolTag.
        DeviceArray m_materials;
    };
}
