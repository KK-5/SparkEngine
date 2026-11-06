#ifndef SPARK_RAY_TRACING_SCENE_BINDINGS_HLSL
#define SPARK_RAY_TRACING_SCENE_BINDINGS_HLSL

// Matches RayTracingBindingSystem::Init and kRayTracingSpaceId.
RaytracingAccelerationStructure g_RayTracingScene : register(t0, space7);

#endif
