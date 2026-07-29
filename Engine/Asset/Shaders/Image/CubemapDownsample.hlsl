// CubemapDownsample.hlsl — one mip level of a cubemap's mip chain, 2x2 box filter.
//
// Dispatched once per level: Dispatch(dstFaceRes, dstFaceRes, 6), reading mip N-1 and
// writing mip N. The array dimension is the six CUBE FACES (id.z), not mip levels — a
// UAV descriptor binds to exactly one mip slice, so the level is chosen on the CPU side
// by which view is bound, never in the shader.
//
// Both source and destination are UAVs, which looks wrong at first glance — the obvious
// spelling is an SRV + SampleLevel on the source. It is deliberate: the engine's barriers
// are whole-resource (D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, see
// CommandListBase::QueueTransitionBarrier), so a single mip CANNOT be transitioned to
// shader-read while its sibling stays writable. Keeping the whole chain in UAV state and
// separating levels with UAV barriers is the only correct option here. It costs nothing:
// a box filter reads 4 texels and averages them, so the sampler's bilinear filtering was
// never needed. Do not "optimise" this into an SRV without first giving barriers
// per-subresource ranges.
//
// Face edges are filtered independently (no cross-face neighbourhood), so seams are
// slightly wrong. That is standard for IBL mip chains and invisible at these use sites.

RWTexture2DArray<float4> g_SrcMip : register(u0, space0);   // read  (mip N-1)
RWTexture2DArray<float4> g_DstMip : register(u1, space0);   // write (mip N)

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height, faceCount;
    g_DstMip.GetDimensions(width, height, faceCount);

    if (id.x >= width || id.y >= height || id.z >= faceCount)
    {
        return;
    }

    g_DstMip[id] = (g_SrcMip[uint3(2 * id.x,     2 * id.y,     id.z)] +
                    g_SrcMip[uint3(2 * id.x + 1, 2 * id.y,     id.z)] +
                    g_SrcMip[uint3(2 * id.x,     2 * id.y + 1, id.z)] +
                    g_SrcMip[uint3(2 * id.x + 1, 2 * id.y + 1, id.z)]) * 0.25f;
}