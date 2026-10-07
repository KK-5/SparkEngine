#pragma once

#include <cstdint>

#include <ECS/WorldContext.h>

#include <RHI/Context/RHIContext.h>

#include "DrawTag.h"

namespace Spark::Render
{
    //! The classification tags of one object, a bit each: InstanceData::m_drawMask. It lets
    //! a reader of g_Instances ask what a Scope asks with Accepts<Tag>(). 0 draws nowhere.
    using DrawMask = uint32_t;

    //! The bit of a tag. Only a tag something reads g_Instances for has one.
    template<typename Tag> struct DrawMaskBit;
    template<> struct DrawMaskBit<OpaqueTag>       { static constexpr DrawMask Value = 1u << 0; };
    template<> struct DrawMaskBit<ShadowCasterTag> { static constexpr DrawMask Value = 1u << 1; };

    template<typename... Tags>
    constexpr DrawMask DrawMaskOf()
    {
        return (DrawMask{0} | ... | DrawMaskBit<Tags>::Value);
    }

    //! What a world entity with a mesh is drawn as. The one source of it: the tags on its
    //! GeometrySpec and its m_drawMask both come from here, so both paths draw the same set.
    DrawMask ClassifyDraw(const WorldContext& world, Entity entity);

    //! Puts on spec the tags mask names.
    void AddDrawTags(RHI::RHIContext& rhiCtx, RHI::RHIHandle spec, DrawMask mask);
}
