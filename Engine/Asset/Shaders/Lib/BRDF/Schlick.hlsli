#ifndef SPARK_LIB_SCHLICK_HLSLI
#define SPARK_LIB_SCHLICK_HLSLI


float F_Schlick(float u, float f0, float f90) {
    return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}

float3 F_Schlick(float u, float3 f0) {
    float f = pow(1.0 - u, 5.0);
    return f + f0 * (1.0 - f);
}

#endif // SPARK_LIB_SCHLICK_HLSLI