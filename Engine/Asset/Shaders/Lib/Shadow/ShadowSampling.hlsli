// Turns a shadow view record plus a world position into a visibility factor. Nothing here is
// bound either: the atlas, its comparison sampler and the atlas texel size all arrive as
// arguments, so a forward pass, a debug visualiser and the deferred lighting pass can share
// this without agreeing on a register.
#ifndef SPARK_LIB_SHADOW_SAMPLING_HLSLI
#define SPARK_LIB_SHADOW_SAMPLING_HLSLI

#include <Shaders/Lib/Shadow/ShadowViewData.hlsli>
#include <Shaders/Lib/Shadow/BicubicPcf.hlsli>

//! face = axis * 2 + negative, for a vector pointing from the light at what it lights.
//! ShadowViewSystem::CubeFaceDirection is the same encoding read the other way, and the two
//! must agree — nothing else about a face is shared, its orientation included.
uint CubeFaceIndex(float3 v)
{
    float3 a    = abs(v);
    uint   axis = (a.x >= a.y && a.x >= a.z) ? 0 : (a.y >= a.z ? 1 : 2);
    return axis * 2 + (v[axis] < 0.0 ? 1 : 0);
}

//! 1 = lit, 0 = fully shadowed. The tile transform is already inside worldToShadowUV, so
//! nothing here knows the atlas layout.
float SampleShadow(ShadowViewData sv, float3 worldPos, float3 N,
                   Texture2D atlas, SamplerComparisonState smp, float invAtlasSize)
{
    // The offset is authored in texels, and a texel's world size scales with w. Taken at the
    // unoffset position: the offset is a texel or two against a distance of many, and solving
    // for the w it would itself produce buys nothing.
    float w = mul(sv.worldToShadowUV, float4(worldPos, 1.0)).w;
    float3 p = worldPos + N * (sv.normalOffsetTexels * sv.texelWorldSizePerW * w);

    float4 clip = mul(sv.worldToShadowUV, float4(p, 1.0));
    float3 uvz  = clip.xyz / clip.w;

    if (uvz.z <= 0.0 || uvz.z >= 1.0)
    {
        return 1.0;
    }
    if (any(uvz.xy < sv.uvMinMax.xy) || any(uvz.xy > sv.uvMinMax.zw))
    {
        return 1.0;
    }

    ShadowFilterInput input;
    input.uv           = uvz.xy;
    input.z            = uvz.z - sv.depthBias;
    input.atlasSize    = 1.0 / invAtlasSize;
    input.invAtlasSize = invAtlasSize;

    // Half a texel in from the inset rect, because its bounds are texel EDGES: a tap clamped
    // to one still draws half its bilinear weight from the unrasterized border, which holds
    // the clear depth and so reads as unshadowed.
    float halfTexel = 0.5 * invAtlasSize;
    input.uvClamp = sv.uvMinMax + float4(halfTexel, halfTexel, -halfTexel, -halfTexel);

    return BicubicPcf(atlas, smp, input, sv.pcfRadiusTexels);
}

#endif // SPARK_LIB_SHADOW_SAMPLING_HLSLI
