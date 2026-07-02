// EnvironmentBake.hlsl — equirectangular HDRI → cubemap, on the compute pipeline.
//
// One dispatch covers all six faces: Dispatch(faceRes, faceRes, 6). Each thread
// owns one cube texel (id.xy = texel, id.z = face), reconstructs the world-space
// direction for that texel using the standard D3D TextureCube face basis, maps the
// direction to equirect UV, samples the source, and writes the face.
//
// The face basis below is the D3D cubemap convention, so the runtime hardware
// TextureCube.Sample(dir) round-trips to the same texel we wrote here — no extra
// pairing logic is needed on the sampling side.
//
// Compute has no implicit pixel-quad derivatives, so the equirect sample uses
// SampleLevel(..., 0) with an explicit LOD rather than Sample().

Texture2D<float4>        g_Equirect : register(t0, space0);
SamplerState             g_Sampler  : register(s0, space0);
RWTexture2DArray<float4> g_Cube     : register(u0, space0);

static const float PI     = 3.14159265358979323846;
static const float TWO_PI = 6.28318530717958647692;

// Face f, with s,t in [-1,1] (horizontal, vertical across the face). Returns the
// (un-normalized) world direction per the D3D cube face convention.
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

// World direction → equirectangular UV. v=0 is the top (latitude +90°), matching
// stbi_loadf's top-to-bottom row order for the source HDRI.
float2 DirectionToEquirectUV(float3 dir)
{
    dir = normalize(dir);
    float lon = atan2(dir.z, dir.x);          // [-PI, PI]
    float lat = asin(clamp(dir.y, -1.0, 1.0)); // [-PI/2, PI/2]

    return float2(0.5 - lon / TWO_PI, 0.5 - lat / PI);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height, faceCount;
    g_Cube.GetDimensions(width, height, faceCount);
    if (id.x >= width || id.y >= height || id.z >= faceCount)
    {
        return;
    }

    // Texel center → [-1,1] across the face.
    float s = 2.0 * (float(id.x) + 0.5) / float(width)  - 1.0;
    float t = 2.0 * (float(id.y) + 0.5) / float(height) - 1.0;

    float3 dir = FaceDirection(id.z, s, t);
    float2 uv  = DirectionToEquirectUV(dir);

    g_Cube[id] = g_Equirect.SampleLevel(g_Sampler, uv, 0.0);
}
