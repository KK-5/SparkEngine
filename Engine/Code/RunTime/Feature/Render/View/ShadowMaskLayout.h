#pragma once

#include <cstdint>

#include <RHI/Format.h>

namespace Spark::Render
{
    //! ShadowMask is the screen-space visibility signal the lighting passes read instead of
    //! sampling the shadow atlas: one [0,1] scalar per (pixel, light). It exists so something
    //! can sit between computing visibility and using it -- denoising ray-traced shadows,
    //! multiplying in contact shadows, swapping the source per light. A light whose shadows
    //! need none of that would be cheaper sampled inline, but whether it needs them is a
    //! per-light setting, so every shadow-casting light gets a slot.
    //!
    //! Four lights share one RGBA8 slice. Packing saves no memory -- one byte per light per
    //! pixel either way -- it saves slices and fetches: the lighting loop reads four lights'
    //! masks in one 32-bit load.

    //! Lights per slice. Not a free parameter forever: a subsurface shading model needs a
    //! second scalar per light (the transmission term has to be found at projection time),
    //! and the choice then is to halve this or to give subsurface lights a texture of their
    //! own. Keep the shifts behind SampleShadowMask so it stays one decision.
    inline constexpr uint32_t kShadowMaskPackWidth = 4;

    //! Budget, in slices. Memory is one byte per light per pixel, so this is 16 lights and
    //! 31.6 MiB at 1080p -- and it scales with render resolution, so 4K wants revisiting.
    //! Lights past it get no slot and cast no shadow; ordering by importance, and what to do
    //! about the overflow, waits for light culling, which turns the ceiling into a per-pixel
    //! one and changes the shape of the question.
    inline constexpr uint32_t kShadowMaskSliceMax = 4;

    inline constexpr uint32_t kShadowMaskCapacity = kShadowMaskPackWidth * kShadowMaskSliceMax;

    inline constexpr RHI::Format kShadowMaskFormat = RHI::Format::R8G8B8A8_UNORM;

    //! On the WORLD light entity: which mask slot it was granted this frame. Absent means
    //! the light casts no shadow, or the budget ran out. A component rather than a number
    //! handed between two loops, so nothing depends on both visiting lights in one order.
    struct ShadowMaskSlot
    {
        uint32_t m_index = 0;
    };

    //! On the draw entity ShadowMaskSystem owns: slices in use this frame. The projection
    //! pass reads it for the attachment's array size, so the array and the draw's instance
    //! count come from one value.
    struct ShadowMaskLayout
    {
        uint32_t m_sliceCount = 0;
    };

    //! Marks that draw entity as ShadowMaskSystem's, so shutdown can find it. The entity
    //! carries its own DrawItem and pass tag and never passes through DrawItemRouter --
    //! a draw whose instance count is a per-frame quantity has nothing to derive once.
    struct ShadowMaskDrawTag {};
}
