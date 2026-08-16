#ifndef SPARK_LIB_VISIBILITY_HLSLI
#define SPARK_LIB_VISIBILITY_HLSLI

// Specular Anti-Aliasing technique from this paper:
// http://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
float CalculateSpecularAA(float3 normal, float roughnessA2)
{
    // Constants for formula below
    const float screenVariance = 0.25f;
    const float varianceThresh = 0.18f;

    // Specular Anti-Aliasing
    float3 dndu = ddx_fine( normal );
    float3 dndv = ddy_fine( normal );
    float variance = screenVariance * (dot( dndu , dndu ) + dot( dndv , dndv ));
    float kernelRoughnessA2 = min(2.0 * variance , varianceThresh );
    float filteredRoughnessA2 = saturate ( roughnessA2 + kernelRoughnessA2 );
    return filteredRoughnessA2;
}

float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
    float a2 = roughness * roughness;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}

float V_SmithGGXCorrelatedFast(float NoV, float NoL, float roughness) {
    float a = roughness;
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

#endif // SPARK_LIB_VISIBILITY_HLSLI