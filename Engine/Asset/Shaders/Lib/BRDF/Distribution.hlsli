#ifndef SPARK_LIB_DISTRIBUTION_HLSLI
#define SPARK_LIB_DISTRIBUTION_HLSLI

#include <Shaders/Lib/BRDF/Constants.hlsli>

float D_GGX(float NoH, float roughness) {
    float a = NoH * roughness;
    float k = roughness / (1.0 - NoH * NoH + a * a);
    return k * k * (1.0 / PI);
}

#endif // SPARK_LIB_DISTRIBUTION_HLSLI