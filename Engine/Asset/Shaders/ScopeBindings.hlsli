// Per-Scope shader inputs, reserved at register space5, the per-Scope tier (all tiers:
// Render/Binding/BindingSpaces.h). Each Scope of a pass gets its own block, carried
// as root constants (at most 128 bytes): heap indices from .BindIndex and small values
// from .Constant, matched by field name.
//
// The fields are the shader's own. Define them first, then include:
//     struct ScopeParameters { uint inputIndex; uint2 outputSize; };
//     #include <Shaders/ScopeBindings.hlsli>
#ifndef SPARK_SCOPE_BINDINGS_HLSL
#define SPARK_SCOPE_BINDINGS_HLSL

[[vk::push_constant]] ConstantBuffer<ScopeParameters> g_Scope : register(b0, space5);

#endif // SPARK_SCOPE_BINDINGS_HLSL
