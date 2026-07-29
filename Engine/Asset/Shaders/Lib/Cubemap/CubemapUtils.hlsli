#ifndef SPARK_LIB_CUBEMAP_HLSLI
#define SPARK_LIB_CUBEMAP_HLSLI

// D3D TextureCube face basis. face f, s/t in [-1,1] across the face (s horizontal,
// t vertical). Returns the UN-normalized world direction. MUST stay bit-identical to the
// basis EnvironmentBake bakes the sky with, or the three IBL cubes disagree on orientation
// — a "lighting comes from the wrong direction" bug that is very hard to localize.
float3 FaceDirection(uint face, float s, float t)
{
    switch (face)
    {
    case 0: return float3( 1.0,  -t,  -s); // +X
    case 1: return float3(-1.0,  -t,   s); // -X
    case 2: return float3(  s,  1.0,   t); // +Y
    case 3: return float3(  s, -1.0,  -t); // -Y
    case 4: return float3(  s,  -t,  1.0); // +Z
    default:return float3( -s,  -t, -1.0); // -Z
    }
}

// Normalized world direction for the CENTER of output cube texel `texel` on `face`, given
// the face resolution. This is the N (irradiance) or R (prefilter) that texel represents.
float3 CubeTexelDirection(uint face, uint2 texel, uint faceRes)
{
    float s = 2.0 * (float(texel.x) + 0.5) / float(faceRes) - 1.0;
    float t = 2.0 * (float(texel.y) + 0.5) / float(faceRes) - 1.0;
    return normalize(FaceDirection(face, s, t));
}


#endif // SPARK_LIB_CUBEMAP_HLSLI