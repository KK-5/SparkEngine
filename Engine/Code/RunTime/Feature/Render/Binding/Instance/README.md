# Instance lists

`InstanceBindingSystem` owns two packed lists of `uint32_t` instance slots:

| Buffer tag | Shader access | Valid prefix |
|---|---|---|
| `InstanceIDBufferTag` | Existing per-pass instance ID input / vertex stream | `ActiveInstanceCount` |
| `EmissiveInstanceIDBufferTag` | `g_EmissiveInstanceIDs`, t1 space4 in `InstanceBindings.hlsli` | `EmissiveInstanceCount` |

Both counts are components on the system's count entity in the RHI context. The
consumer passes the emissive count through its own scope constants; the buffer view
describes capacity, not the number of live entries. An empty list has count zero.

Emissive candidates are collected during the normal CPU instance encoding each
frame. They use the same resolved material as `InstanceData`: a synthesized override
first, then the referenced material, then the resident default. A candidate needs a
GPU material slot, a geometry slot, uploaded geometry, and at least one positive
component of emissive RGB times emissive strength. Dead entities are excluded.

The list stores only instance slots. Each slot already identifies the current mesh,
transform and material. Emissive textures remain supported by material evaluation
at the sampled surface point; the collection neither inspects texels nor requires
texture residency. A black texture may leave a zero-contribution candidate.

CPU collection is rebuilt each frame, while `ActiveInstanceList` stages only changed
positions. Shrinking the list only changes its valid count and retires unused pages.
The order is not an identity and can change when instances or materials change.

A consumer declares `InstanceBindingTag`, imports `EmissiveInstanceIDBufferTag`, and
declares its shader read in each scope that uses the list. It also declares reads of
the instance, geometry and material resources it subsequently accesses. Buffer
uploads are registered by `RenderSystem::SetUpSystemPasses`.

This is a list of renderable emissive candidates, not a TLAS membership list or a
light-sampling distribution. A ray-traced consumer must respect that frame's RT
scene readiness and supported surface policy when adding light sampling.
