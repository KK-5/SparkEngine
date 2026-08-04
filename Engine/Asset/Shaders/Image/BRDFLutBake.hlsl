// BRDFLutBake.hlsl — the split-sum approximation's second factor, pre-integrated into a
// 2D table: the specular BRDF against a white environment, as the (scale, bias) pair
// applied to F0. Scene-independent, so this is baked ONCE offline (SandBox BRDFLutGen)
// and the .ktx2 is checked in -- the runtime only loads it.
//
// Integrated against this repo's V_SmithGGXCorrelated, which is the whole point of baking
// it here: the widely circulated Karis/UE4 tables use a non-height-correlated Smith and
// read a few percent off against the direct-light specular beside them.
//
// UV CONVENTION (must match the sampling side exactly, and nothing fails if it does not --
// metals just come out systematically off): u = NoV, v = perceptualRoughness, both at
// TEXEL CENTRES. The centres are load-bearing twice over: x=0 would give NoV=0 and blow up
// the visibility term, and it is what lets the shader sample at plain uv=(NoV, roughness)
// with no half-texel correction.

#include <Shaders/Lib/Sample/Sample.hlsli>
#include <Shaders/Lib/BRDF/ImportanceSample.hlsli>
#include <Shaders/Lib/BRDF/Visibility.hlsli>

RWTexture2D<float2> g_Lut : register(u0, space0);

cbuffer BRDFLutParams : register(b0, space0)
{
    uint g_SampleCount;
};

float2 IntegrateDFG(float NoV, float perceptualRoughness)
{
    // N fixed at +z; NoV then fixes V up to an azimuth the integral is symmetric in.
    float3 N = float3(0.0, 0.0, 1.0);
    float3 V = float3(sqrt(saturate(1.0 - NoV * NoV)), 0.0, NoV);
    float  alpha = perceptualRoughness * perceptualRoughness;

    float A = 0.0;
    float B = 0.0;
    for (uint i = 0u; i < g_SampleCount; ++i)
    {
        float2 xi = Hammersley(i, g_SampleCount);
        float3 H  = ImportanceSampleGGX(xi, N, perceptualRoughness);
        float3 L  = 2.0 * dot(V, H) * H - V;

        float NoL = saturate(L.z);
        if (NoL <= 0.0)
        {
            continue;
        }

        float NoH = saturate(H.z);
        float VoH = saturate(dot(V, H));

        // Estimator with pdf(L) = D*NoH/(4*VoH): D*Vis*F*NoL / pdf = 4*Vis*NoL*VoH/NoH * F.
        // Karis writes the same factor as G*VoH/(NoH*NoV); this repo stores the visibility
        // term already divided (Vis = G/(4*NoL*NoV)), so copying his G_Vis verbatim here
        // would leave the result short by 4*NoL*NoV.
        float Vis    = V_SmithGGXCorrelated(NoV, NoL, alpha);
        float weight = 4.0 * Vis * NoL * VoH / max(NoH, 1e-4);

        // F0 factors out of Schlick: F = F0*(1-Fc) + Fc, so one integral per term.
        float Fc = pow(1.0 - VoH, 5.0);
        A += (1.0 - Fc) * weight;
        B += Fc * weight;
    }

    return float2(A, B) / float(g_SampleCount);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    g_Lut.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }

    float NoV                 = (float(id.x) + 0.5) / float(width);
    float perceptualRoughness = (float(id.y) + 0.5) / float(height);

    g_Lut[id.xy] = IntegrateDFG(NoV, perceptualRoughness);
}
