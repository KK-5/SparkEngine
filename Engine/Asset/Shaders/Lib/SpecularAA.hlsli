#ifndef SPARK_LIB_SPECULAR_AA_HLSLI
#define SPARK_LIB_SPECULAR_AA_HLSLI

// Geometric specular anti-aliasing: widens the lobe by the normal's variance across the pixel,
// so a highlight narrower than a pixel is prefiltered instead of aliasing.
// http://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
//
// Pixel shader only, on a surface's own interpolated normal: the screen-space derivatives
// must span one surface, which a full-screen pass reading a GBuffer does not guarantee.
float CalculateSpecularAA(float3 normal, float roughnessA2)
{
    const float screenVariance = 0.25f;
    const float varianceThresh = 0.18f;

    float3 dndu = ddx_fine(normal);
    float3 dndv = ddy_fine(normal);
    float variance = screenVariance * (dot(dndu, dndu) + dot(dndv, dndv));
    float kernelRoughnessA2 = min(2.0 * variance, varianceThresh);
    return saturate(roughnessA2 + kernelRoughnessA2);
}

#endif // SPARK_LIB_SPECULAR_AA_HLSLI
