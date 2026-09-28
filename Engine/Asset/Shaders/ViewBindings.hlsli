// Per-view shader inputs (the "ViewBindings" group), reserved at register space1: one
// g_Views row per live view, at the view's stable slot. Shared engine header — any shader
// that needs camera/view data does:
//     #include <Shaders/ViewBindings.hlsli>
// and reads a view by index, GetView(g_Scope.viewIndex) for the one being rendered (see
// ScopeBindings.hlsli). The pass declares .Binds<ViewBindingTag>(); ViewBindingSystem
// fills the rows.
//
// Binding-space convention (by update frequency, low = stable): space0 per-scene,
// space1 per-view, space2 per-pass, space3 per-material, space4 per-object, space5 per-Scope.
#ifndef SPARK_VIEW_BINDINGS_HLSL
#define SPARK_VIEW_BINDINGS_HLSL

#include "ViewData.hlsli"

StructuredBuffer<ViewData> g_Views : register(t0, space1);

ViewData GetView(uint viewIndex)
{
    return g_Views[viewIndex];
}

//! Device depth -> view-space depth, for the reversed-Z perspective and orthographic
//! projections Math::PerspectiveFov / OrthographicProjection build.
float ConvertFromDeviceZ(ViewData view, float deviceZ)
{
    return view.invDeviceZToViewZ.z > 0.5
        ? view.invDeviceZToViewZ.y / (deviceZ - view.invDeviceZToViewZ.x)
        : (deviceZ - view.invDeviceZToViewZ.y) / view.invDeviceZToViewZ.x;
}

#endif // SPARK_VIEW_BINDINGS_HLSL
