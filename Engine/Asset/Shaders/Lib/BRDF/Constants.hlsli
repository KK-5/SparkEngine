// Shared BRDF constants. Every term header (Diffuse / Specular / Schlick ...) pulls
// PI and friends from here so no single value is defined twice in one translation
// unit — include this instead of re-declaring a local `static const`.
#ifndef SPARK_LIB_BRDF_CONSTANTS_HLSLI
#define SPARK_LIB_BRDF_CONSTANTS_HLSLI

static const float PI = 3.14159265359;

#endif // SPARK_LIB_BRDF_CONSTANTS_HLSLI
