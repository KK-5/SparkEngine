#pragma once

#include <RHI/Resource/AccessFlags.h>
#include <Skybox/Components.h>

#include <Pass/TagList.h>

namespace Spark::Render
{
    //! On the lookup tables SceneBindingSystem loads and binds into space0: the BRDF LUT and
    //! the two LTC tables.
    struct SceneLookupTableTag
    {
        static constexpr RHI::AccessFlags Access = RHI::AccessFlags::ShaderSampledRead;
    };

    //! Marks the single shared per-scene ShaderBindings entity (space0): the g_Lights
    //! StructuredBuffer + scene constants. A pass declares .Binds<MainSceneTag>() and the
    //! executer binds it once before the pass's draws, exactly like ViewBindingTag.
    //! Produced and owned by SceneBindingSystem.
    struct MainSceneTag
    {
        using Uses = TagList<Skybox::SkyLightingTag, SceneLookupTableTag>;
    };
}
