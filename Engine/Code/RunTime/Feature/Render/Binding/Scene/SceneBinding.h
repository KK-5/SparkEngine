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
    //!
    //! No Uses: the scene's resources are read through explicit accesses now, and those
    //! acquire their uploads themselves. A scope-level take here would acquire the same
    //! upload before the access does, and the access would then transition from the state
    //! the take left behind rather than the one it recorded.
    struct MainSceneTag
    {
        using Uses = TagList<>;
    };

    //! This frame's g_Lights / g_ShadowViews / g_ViewShadowMaps buffer, on the staging slot it
    //! was written into. The slots rotate, so each tag follows the one the frame writes.
    struct LightBufferTag
    {
    };

    struct ShadowViewBufferTag
    {
    };

    struct ViewShadowMapBufferTag
    {
    };
}
