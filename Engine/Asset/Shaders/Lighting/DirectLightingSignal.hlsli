// Engine-owned signal convention, independent of an NRD SDK version. Both the
// producer and composition must use these same nonzero material factors.
#ifndef SPARK_DIRECT_LIGHTING_SIGNAL_HLSLI
#define SPARK_DIRECT_LIGHTING_SIGNAL_HLSLI

static const float2 kDirectLightingInvalidMotion = float2(2.0, 2.0);

void DirectLightingMaterialFactors(float3 diffuseColor, float3 specularColor,
                                   out float3 diffuseFactor, out float3 specularFactor)
{
    // Remove primary-surface color before filtering. A floor preserves grazing
    // Fresnel when F0 is zero and avoids division by zero on black/metal surfaces.
    diffuseFactor = max(diffuseColor, 0.02);
    specularFactor = max(specularColor, 0.02);
}

#endif
