// Screen-space shadow mask: one [0,1] visibility scalar per (pixel, light), four lights
// packed into one RGBA8 slice. C++ mirror of the constants lives in
// Render/View/ShadowMaskLayout.h.
//
// The slot is what a light carries, not a global light index -- once light culling packs
// the mask per cluster it becomes a per-cluster slot and only this file changes.
//
// Nothing here is bound: the texture arrives as an argument.
#ifndef SPARK_LIB_SHADOW_MASK_HLSLI
#define SPARK_LIB_SHADOW_MASK_HLSLI

#define SPARK_SHADOW_MASK_PACK_WIDTH 4

uint ShadowMaskSlice(int slot)   { return uint(slot) / SPARK_SHADOW_MASK_PACK_WIDTH; }
uint ShadowMaskChannel(int slot) { return uint(slot) % SPARK_SHADOW_MASK_PACK_WIDTH; }

//! 1 = lit. Callers gate on slot >= 0 first: a light with no slot casts no shadow.
float SampleShadowMask(Texture2DArray mask, int2 pixelPos, int slot)
{
    return mask.Load(int4(pixelPos, ShadowMaskSlice(slot), 0))[ShadowMaskChannel(slot)];
}

#endif // SPARK_LIB_SHADOW_MASK_HLSLI
