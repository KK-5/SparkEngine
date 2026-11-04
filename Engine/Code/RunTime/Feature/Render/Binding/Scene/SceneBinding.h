#pragma once

#include <RHI/Resource/AccessFlags.h>
#include <Scene/SceneTextureComponents.h>

#include <Pass/TagList.h>

namespace Spark::Render
{
    //! Marks the single shared per-scene ShaderBindings entity (space0): the g_Lights
    //! StructuredBuffer + scene constants. A pass declares .Binds<MainSceneTag>() and the
    //! executer binds it once before the pass's draws, exactly like ViewBindingTag.
    //! Produced and owned by SceneBindingSystem.
    struct MainSceneTag
    {
        using Uses = TagList<SkyLightingTag, SceneLookupTableTag>;
    };
}
