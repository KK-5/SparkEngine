#pragma once

namespace Spark::Render
{
    //! Marks the single shared per-scene ShaderBindings entity (space0): the g_Lights
    //! StructuredBuffer + scene constants. A pass declares .Binds<MainSceneTag>() and the
    //! executer binds it once before the pass's draws, exactly like MainViewTag.
    //! Produced and owned by SceneBindingSystem.
    struct MainSceneTag {};
}
