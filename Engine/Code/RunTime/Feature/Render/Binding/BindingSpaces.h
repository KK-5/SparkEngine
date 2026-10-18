#pragma once

#include <Resource/Shader/ShaderAsset.h>

namespace Spark::Render
{
    // The HLSL register spaces of the engine's binding tiers, the one list of them: the
    // .hlsli headers write the same numbers in their register(), and a pass's layout is
    // checked against these. Assigned by update frequency, low = stable; space6 extends
    // the convention past the per-Scope tier.
    //
    // Only for shaders written against the engine's groups. A standalone shader (the
    // EnvironmentBaker's, the SandBox tools') numbers its own spaces.

    //! SceneBindings.hlsli: lights, shadow views, environment, LUTs. SceneBindingSystem.
    inline constexpr uint32_t kSceneSpaceId = 0;

    //! ViewBindings.hlsli: g_Views. ViewBindingSystem.
    inline constexpr uint32_t kViewSpaceId = 1;

    //! A pass's OWN per-pass ShaderBindings tier (g_SceneColor, g_SkyCube, g_MatSampler, …).
    //! RenderPassBuilder::Finalize auto-creates it when the reflected layout declares it,
    //! so no pass processor has to allocate it.
    inline constexpr uint32_t kPerPassSpaceId = 2;

    //! MaterialBindings.hlsli: g_Materials. MaterialBindingSystem.
    inline constexpr uint32_t kMaterialSpaceId = 3;

    //! InstanceBindings.hlsli: g_Instances. InstanceBindingSystem.
    inline constexpr uint32_t kInstanceSpaceId = 4;

    //! The per-Scope tier (ScopeBindings.hlsli): each Scope's own block, set by its
    //! .Constant / .BindIndex. Root constants carry it, hence the shader layer's id.
    inline constexpr uint32_t kPerScopeSpaceId = Resource::RootConstantsSpaceId;

    //! GeometryBindings.hlsli: g_Geometries. GeometryBindingSystem.
    inline constexpr uint32_t kGeometrySpaceId = 6;
}
