# ReferencePathTracer

The first stage of the reference tracer: one fixed primary ray per pixel, closest-hit
geometry reconstruction and shared StandardPBR material evaluation at explicit LOD 0.
The default view uses a diagnostic camera headlight and displays emissive materials.
It does not yet integrate the ceiling light, trace indirect bounces, accumulate frames
or denoise. In particular, this stage does not produce Cornell Box color bleeding or
the blocks' cast shadows.

The original, Cornell Box-style sample asset uses meters, Y up, a room spanning
X = [-1, 1], Y = [0, 2], Z = [0, 2], and an open front at Z = 0. It has a red left
wall, green right wall, white surfaces, two rotated blocks and an emissive ceiling
panel (emissive RGB = 1). After synchronously loading the model on the main thread,
the scene helper broadcasts `AssetResolveBus::ResolveModelAssetToScene`.
`ModelSpawnResolver` / `SpawnModel` create the node hierarchy, mesh children and
material references. The sample enables `HierarchyManager`, `TransformSystem` and
`MaterialSystem`; the transform tick produces the world matrices consumed by RT.
WorldContext owns all spawned entities until application teardown.
The dimensions and colors are illustrative, not measured Cornell reference data.

```powershell
cmake --build build --config Debug --target ReferencePathTracer -j 4
.\bin\Debug\ReferencePathTracer.exe
# Optionally select the initial debug view (1-6).
.\bin\Debug\ReferencePathTracer.exe --mode 2
```

The CMake option `BUILD_REFERENCEPATHTRACER` defaults to ON. The sample requires
hardware inline ray queries, just like the existing RayTracingScene sample.

| Key / --mode | View | Check |
|---|---|---|
| 1 | Surface | Shared BRDF, interpolated normal, emissive |
| 2 | Base color | Per-instance material indices and authored color factors |
| 3 | Shading normal | Interpolated vertex normal after model import |
| 4 | Geometric normal | Triangle indices, winding and transformed edges |
| 5 | UV / checker | Interpolated UV in RG, 8x8 checker in B |
| 6 | Material inputs | R = roughness, G = metallic, B = specular |

Escape closes the sample. Normal/UV/material diagnostic views show raw channel
values; BaseColor uses linear-to-sRGB conversion, Surface adds Reinhard tone mapping.
White surfaces can merge visually in BaseColor mode; use Surface or the normal views
to inspect the blocks' shape. For this flat-shaded geometry, the two normal views
should match apart from numerical precision.
The loader bakes the glTF node transforms and converts Z from right-handed glTF to
left-handed engine space. Each node therefore references a unique mesh, even though
the source accessors share the same unit cube. Runtime instance matrices start at
identity; transformed-instance normal tests will need additional scene coverage.
These flat materials exercise factor evaluation; textured materials and normal maps
will need additional scene coverage. The render graph declares the TLAS and all four
geometry/instance/material buffer reads and acquires material textures through the
shared MaterialBindingTag.
