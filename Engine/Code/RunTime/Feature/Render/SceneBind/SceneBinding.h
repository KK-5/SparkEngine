#pragma once

namespace Spark::Render
{
    //! Marks the single shared per-scene ShaderBindings entity (space0): the g_Lights
    //! StructuredBuffer + scene constants. A pass declares .Binds<MainSceneTag>() and
    //! BindPassDrawItems injects it, exactly like MainViewTag for the per-view group.
    //! Produced and owned by SceneBindingSystem.
    struct MainSceneTag {};
}
