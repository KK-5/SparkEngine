#pragma once

namespace Spark::Render
{
    //! Marks the single shared per-scene ShaderBindings entity (space0): the g_Lights
    //! StructuredBuffer + scene constants. Consumers pull it into their draw requests via
    //! AddShaderBindings<MainSceneTag>, exactly like MainViewTag for the per-view group.
    //! Produced and owned by SceneBindingSystem.
    struct MainSceneTag {};
}
