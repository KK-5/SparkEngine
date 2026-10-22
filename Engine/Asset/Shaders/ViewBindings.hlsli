// Per-view shader inputs (the "ViewBindings" group), reserved at register space1: one
// g_Views row per live view, at the view's stable slot. Shared engine header — any shader
// that needs camera/view data does:
//     #include <Shaders/ViewBindings.hlsli>
// and reads a view by index, GetView(g_Scope.viewIndex) for the one being rendered (see
// ScopeBindings.hlsli). The pass declares .Binds<ViewBindingTag>(); ViewBindingSystem
// fills the rows.
//
// The spaces of all binding tiers are listed in Render/Binding/BindingSpaces.h.
#ifndef SPARK_VIEW_BINDINGS_HLSL
#define SPARK_VIEW_BINDINGS_HLSL

#include "ViewData.hlsli"

// The group's layout is declared again in ViewBindingSystem::Init; change both together.
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

//! A pixel shader's SV_Position back to the world, through the jittered matrix it was
//! rasterized with.
float3 SvPositionToWorld(ViewData view, float4 svPosition)
{
    float2 uv  = (svPosition.xy - view.viewRectMin.xy) * view.viewSizeAndInvSize.zw;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 worldH = mul(view.invViewProj, float4(ndc, svPosition.z, 1.0));
    return worldH.xyz / worldH.w;
}

#endif // SPARK_VIEW_BINDINGS_HLSL
