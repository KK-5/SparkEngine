// Sampling for the engine's opaque, isotropic Burley diffuse + GGX reflection BRDF.
// No material/scene bindings: all inputs are already evaluated surface parameters.
#ifndef SPARK_LIB_BRDF_SAMPLING_HLSLI
#define SPARK_LIB_BRDF_SAMPLING_HLSLI

#include <Shaders/Lib/BRDF/BRDF.hlsli>

struct BRDFSample
{
    float3 Direction; // world-space direction away from the surface
    float3 Response; // FULL (diffuse + specular) BRDF * shading-normal cosine
    float Pdf; // FULL mixture density with respect to solid angle, in world space
    // Path throughput is multiplied by Response / Pdf; do not multiply cosine again.
};

// Proposal probabilities only; these do not scale the material's BRDF lobes.
// Fresnel at the view angle is an inexpensive estimate of specular importance.
float BRDFSpecularSelectionPmf(float NoV, float3 diffuseColor, float3 F0)
{
    const float3 luminanceWeights = float3(0.2126, 0.7152, 0.0722);
    const float diffuseWeight = dot(diffuseColor, luminanceWeights);
    if (diffuseWeight <= 0.0)
    {
        return 1.0;
    }
    const float specularWeight = dot(F_Schlick(saturate(NoV), F0), luminanceWeights);
    // Preserve both proposals when diffuse is present. Even F0 == 0 can have a
    // nonzero Schlick reflection lobe away from normal incidence.
    return clamp(specularWeight / (diffuseWeight + specularWeight), 0.05, 0.95);
}

// Isotropy lets us construct a frame from N alone; no mesh tangent is required.
void BuildBRDFSamplingFrame(float3 N, out float3 tangent, out float3 bitangent)
{
    const float3 referenceAxis = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(referenceAxis, N));
    bitangent = cross(N, tangent);
}

// Local +Z hemisphere. The proposal density is localDirection.z / PI, even
// though the diffuse response being integrated is Burley rather than Lambert.
float3 SampleCosineHemisphere(float2 uniformSample)
{
    const float radius = sqrt(uniformSample.x);
    const float azimuth = 2.0 * PI * uniformSample.y;
    return float3(radius * cos(azimuth), radius * sin(azimuth), sqrt(1.0 - uniformSample.x));
}

// GGX visible-normal sampling via the projected-hemisphere construction described
// by Eric Heitz (2018): https://jcgt.org/published/0007/04/01/paper.pdf
// localView is unit length with z > 0. perceptualRoughness is already regularized.
// Returns a local half vector, NOT a reflected direction.
float3 SampleGGXVisibleNormal(float3 localView, float perceptualRoughness, float2 uniformSample)
{
    const float alpha = perceptualRoughness * perceptualRoughness;
    const float3 stretchedView = normalize(float3(alpha * localView.xy, localView.z));

    const float tangentLengthSquared = dot(stretchedView.xy, stretchedView.xy);
    float3 diskAxisX = float3(1.0, 0.0, 0.0);
    if (tangentLengthSquared > 0.0)
    {
        diskAxisX = float3(-stretchedView.y, stretchedView.x, 0.0) * rsqrt(tangentLengthSquared);
    }
    const float3 diskAxisY = cross(stretchedView, diskAxisX);

    const float radius = sqrt(uniformSample.x);
    const float azimuth = 2.0 * PI * uniformSample.y;
    const float projectedX = radius * cos(azimuth);
    const float diskY = radius * sin(azimuth);
    const float visibleBlend = 0.5 * (1.0 + stretchedView.z);
    const float projectedY = lerp(sqrt(max(0.0, 1.0 - projectedX * projectedX)), diskY, visibleBlend);
    const float projectedZ = sqrt(max(0.0, 1.0 - projectedX * projectedX - projectedY * projectedY));
    const float3 hemisphereNormal = projectedX * diskAxisX + projectedY * diskAxisY + projectedZ * stretchedView;

    return normalize(float3(alpha * hemisphereNormal.xy, max(0.0, hemisphereNormal.z)));
}

// N, V and L must be normalized. V and L point away from the surface, and N is
// the shading normal. Only reflection above N is supported by the current model.
// Use this same mixture density for both BRDF samples and light-sampled directions.
float EvaluateBRDFPdf(float3 N, float3 V, float3 L,
                      float3 diffuseColor, float3 F0, float perceptualRoughness)
{
    const float NoV = saturate(dot(N, V));
    const float NoL = saturate(dot(N, L));
    if (NoV <= 0.0 || NoL <= 0.0)
    {
        return 0.0;
    }

    perceptualRoughness = max(perceptualRoughness, BRDF_MIN_PERCEPTUAL_ROUGHNESS);
    const float alpha = perceptualRoughness * perceptualRoughness;
    const float alphaSquared = alpha * alpha;
    const float3 H = normalize(V + L);
    const float NoH = saturate(dot(N, H));
    const float diffusePdf = NoL / PI;

    // Visible half-vector density: D(H) * G1(V) * VoH / NoV.
    // Reflection Jacobian: 1 / (4 * VoH).
    // Together: D(H) * G1(V) / (4 * NoV), simplified below to avoid a grazing /0.
    // Use actual NoV here, not the numerical floor used by BRDF evaluation.
    const float smithRoot = sqrt(NoV * NoV + alphaSquared * saturate(1.0 - NoV * NoV));
    const float specularPdf = D_GGX(NoH, alpha) / (2.0 * (NoV + smithRoot));
    const float specularPmf = BRDFSpecularSelectionPmf(NoV, diffuseColor, F0);
    return (1.0 - specularPmf) * diffusePdf + specularPmf * specularPdf;
}

// randomSample.x selects a lobe; .yz samples its shape (e.g. SobolBurley3D).
// Inputs must be uniform in [0,1); colors nonnegative; N and V normalized.
// The same roughness floor as ray-hit evaluation keeps all lobes continuous:
// perfect mirrors and transmission are not part of this BRDF.
bool SampleBRDF(float3 N, float3 V, float3 diffuseColor, float3 F0,
                float perceptualRoughness, float3 randomSample, out BRDFSample brdfSample)
{
    brdfSample = (BRDFSample)0;
    const float NoV = dot(N, V);
    if (NoV <= 0.0)
    {
        return false;
    }
    perceptualRoughness = max(perceptualRoughness, BRDF_MIN_PERCEPTUAL_ROUGHNESS);

    float3 tangent;
    float3 bitangent;
    BuildBRDFSamplingFrame(N, tangent, bitangent);
    float3 localDirection;
    const float specularPmf = BRDFSpecularSelectionPmf(NoV, diffuseColor, F0);
    if (randomSample.x < specularPmf)
    {
        const float3 localView = float3(dot(V, tangent), dot(V, bitangent), NoV);
        const float3 halfVector = SampleGGXVisibleNormal(localView, perceptualRoughness, randomSample.yz);
        localDirection = reflect(-localView, halfVector);
        // VNDF reflection can land below the shading hemisphere. It is a zero
        // contribution; do not retry, or the stated PDF would need renormalization.
        if (localDirection.z <= 0.0)
        {
            return false;
        }
    }
    else
    {
        localDirection = SampleCosineHemisphere(randomSample.yz);
    }

    const float3 L = normalize(localDirection.x * tangent + localDirection.y * bitangent + localDirection.z * N);
    const float pdf = EvaluateBRDFPdf(N, V, L, diffuseColor, F0, perceptualRoughness);
    if (pdf <= 0.0)
    {
        return false;
    }
    brdfSample.Direction = L;
    brdfSample.Pdf = pdf;
    brdfSample.Response = EvaluateBRDF(N, V, L, diffuseColor, F0, perceptualRoughness);
    return true;
}

#endif // SPARK_LIB_BRDF_SAMPLING_HLSLI
