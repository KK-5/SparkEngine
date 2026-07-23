#ifndef SPARK_LIB_DIFFUSE_HLSLI
#define SPARK_LIB_DIFFUSE_HLSLI

#include <Shaders/Lib/BRDF/Constants.hlsli>
#include <Shaders/Lib/BRDF/Schlick.hlsli>

float Fd_Lambert() {
    return 1.0 / PI;
}

// roughness = perceptualRoughness^2 (linear α), same value fed to the specular D/V terms.
float Fd_Burley(float NoV, float NoL, float LoH, float roughness) {
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float lightScatter = F_Schlick(NoL, 1.0, f90);
    float viewScatter = F_Schlick(NoV, 1.0, f90);
    return lightScatter * viewScatter * (1.0 / PI);
}

#endif // SPARK_LIB_DIFFUSE_HLSLI