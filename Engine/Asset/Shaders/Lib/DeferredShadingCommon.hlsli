// GBuffer encode / decode, shared by everything that writes or reads the deferred GBuffer.
// Corresponds to UE's DeferredShadingCommon.ush.
//
// Texture names are semantic here; the UE letters they correspond to are
// GBufferNormal↔GBufferA, GBufferSurface↔GBufferB, GBufferBaseColor↔GBufferC,
// GBufferCustomData↔GBufferD. GBufferData's FIELD names stay identical to UE's FGBufferData
// -- that is what ported shaders reference.
//
// Nothing here is bound: the textures arrive as arguments, so the caller owns every register.
#ifndef SPARK_LIB_DEFERRED_SHADING_COMMON_HLSLI
#define SPARK_LIB_DEFERRED_SHADING_COMMON_HLSLI

#define SHADINGMODELID_UNLIT        0
#define SHADINGMODELID_DEFAULT_LIT  1

struct GBufferData
{
    float3 WorldNormal;
    float3 BaseColor;
    float  Metallic;
    float  Specular;            // dielectric F0 scale; 0.5 == the usual 0.04
    float  Roughness;           // perceptual, unclamped -- consumers clamp as they need
    float  GBufferAO;           // the material's own occlusion map
    float4 CustomData;          // per shading model; always 0 until GBufferCustomData exists
    uint   ShadingModelID;
    uint   SelectiveOutputMask;
    float  Depth;               // device Z

    float3 DiffuseColor;
    float3 SpecularColor;
};

//! Unit vector <-> UNORM. 10 bits per channel, so ~0.1 degrees of worst-case error; swapping
//! in octahedral encoding is a change to these two functions and nothing else.
float3 EncodeNormal(float3 n) { return n * 0.5 + 0.5; }
float3 DecodeNormal(float3 e) { return e * 2.0 - 1.0; }

//! GBufferSurface.a: low 4 bits shading model, high 4 bits selective output mask.
float EncodeShadingModel(uint shadingModelId, uint selectiveOutputMask)
{
    return float((selectiveOutputMask << 4) | shadingModelId) / 255.0;
}

void DecodeShadingModel(float encoded, out uint shadingModelId, out uint selectiveOutputMask)
{
    uint packed = uint(round(encoded * 255.0));
    shadingModelId      = packed & 0xf;
    selectiveOutputMask = packed >> 4;
}

GBufferData GetGBufferData(
    Texture2D gbufferNormal, Texture2D gbufferSurface, Texture2D gbufferBaseColor,
    Texture2D sceneDepth, int2 pixelPos)
{
    int3 px = int3(pixelPos, 0);

    float4 normalSample   = gbufferNormal.Load(px);
    float4 surfaceSample  = gbufferSurface.Load(px);
    float4 baseColorSample = gbufferBaseColor.Load(px);

    GBufferData gbuffer;
    gbuffer.WorldNormal = DecodeNormal(normalSample.xyz);
    gbuffer.BaseColor   = baseColorSample.rgb;
    gbuffer.GBufferAO   = baseColorSample.a;
    gbuffer.Metallic    = surfaceSample.r;
    gbuffer.Specular    = surfaceSample.g;
    gbuffer.Roughness   = surfaceSample.b;
    DecodeShadingModel(surfaceSample.a, gbuffer.ShadingModelID, gbuffer.SelectiveOutputMask);
    gbuffer.CustomData  = float4(0.0, 0.0, 0.0, 0.0);
    gbuffer.Depth       = sceneDepth.Load(px).r;

    // Metals have no diffuse and tint F0 with base color; dielectrics keep a flat F0.
    gbuffer.DiffuseColor  = gbuffer.BaseColor * (1.0 - gbuffer.Metallic);
    gbuffer.SpecularColor = lerp(0.08 * gbuffer.Specular, gbuffer.BaseColor, gbuffer.Metallic);
    return gbuffer;
}

#endif // SPARK_LIB_DEFERRED_SHADING_COMMON_HLSLI
