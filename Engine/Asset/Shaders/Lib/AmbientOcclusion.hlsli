// How indirect lighting applies an ambient occlusion term. The term itself is the material's
// occlusion map times the screen-space signal (SceneTextures::AmbientOcclusion) when the
// frame has one.
#ifndef SPARK_LIB_AMBIENT_OCCLUSION_HLSLI
#define SPARK_LIB_AMBIENT_OCCLUSION_HLSLI

//! Occlusion for indirect diffuse. Plain AO treats every occluded direction as black, but
//! the occluder is lit too and bounces light back, more so the brighter the surfaces: a
//! bright crevice comes out lighter than its AO says. The multi-bounce approximation of
//! Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect Occlusion": a cubic
//! fitted per albedo to a ray-traced reference. Never darker than `ao`.
float3 AOMultiBounce(float3 albedo, float ao)
{
    float3 a =  2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c =  2.7552 * albedo + 0.6903;
    return max(ao, ((ao * a + b) * ao + c) * ao);
}

//! Occlusion for indirect specular, from the diffuse term. A reflection gathers light from a
//! lobe around the mirror direction, not from the whole hemisphere: narrow on a smooth
//! surface, where what occludes the hemisphere mostly misses it, and hemisphere-wide on a
//! rough one, where this tends to `ao`. The specular occlusion of Lagarde and de Rousiers,
//! "Moving Frostbite to Physically Based Rendering" (course notes v3). `roughness` is the
//! perceptual one.
//!
//! Not UE's form, pow(NoV + ao, roughness^2): that is the notes' earlier listing, whose
//! exponent grows with roughness and so gives a mirror the full `ao` and a rough surface less.
float SpecularOcclusion(float NoV, float roughness, float ao)
{
    const float alpha = roughness * roughness;
    return saturate(pow(NoV + ao, exp2(-16.0 * alpha - 1.0)) - 1.0 + ao);
}

#endif // SPARK_LIB_AMBIENT_OCCLUSION_HLSLI
