#pragma once

#include <cstdint>

#include <RHI/Context/RHIContext.h>

#include <Resource/Material/MaterialState.h>

#include "DrawTag.h"

namespace Spark::Render
{
    //! The classification tags of one object, a bit each: InstanceData::m_drawMask. It lets
    //! a reader of g_Instances ask which tags an instance carries. 0 draws nowhere.
    using DrawMask = uint32_t;

    //! The bit of a tag. Only a tag something reads g_Instances for has one.
    template<typename Tag> struct DrawMaskBit;
    template<> struct DrawMaskBit<OpaqueTag>       { static constexpr uint32_t Index = 0; static constexpr DrawMask Value = 1u << Index; };
    template<> struct DrawMaskBit<ShadowCasterTag> { static constexpr uint32_t Index = 1; static constexpr DrawMask Value = 1u << Index; };
    template<> struct DrawMaskBit<TranslucentTag>  { static constexpr uint32_t Index = 2; static constexpr DrawMask Value = 1u << Index; };

    //! How many tags have a bit: an array with an entry per bit has this many.
    inline constexpr uint32_t DrawMaskBitCount = 3;

    template<typename... Tags>
    constexpr DrawMask DrawMaskOf()
    {
        return (DrawMask{0} | ... | DrawMaskBit<Tags>::Value);
    }

    template<typename... Tags>
    constexpr DrawMask DrawMaskCulledOn(DrawCulling culling)
    {
        return (DrawMask{0} | ... | (Tags::Culling == culling ? DrawMaskBit<Tags>::Value : DrawMask{0}));
    }

    //! The bits of the tags the CPU culls, whose lists are their instances rather than their keys.
    inline constexpr DrawMask CpuCulledDrawMask =
        DrawMaskCulledOn<OpaqueTag, ShadowCasterTag, TranslucentTag>(DrawCulling::Cpu);

    //! The draw lists an instance drawn with a material of `state` is in, a bit a list: what the
    //! encoding writes as its m_drawMask. A blended surface is translucent and casts no shadow;
    //! any other is opaque and casts.
    DrawMask DrawListsOf(const Resource::MaterialState& state);

    //! Puts on spec the tags mask names.
    void AddDrawTags(RHI::RHIContext& rhiCtx, RHI::RHIHandle spec, DrawMask mask);
}
