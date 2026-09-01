#pragma once

#include <cstdint>

#include <EASTL/string.h>

namespace Editor
{
    //! Whether a material entity is still there -- i.e. still carries StandardPBR. A handle
    //! is ABA-safe, but material entities CAN be destroyed (the ones an override is composed
    //! into are), so anything holding one across frames has to ask rather than assume.
    //!
    //! Takes the handle as a raw uint32 like the reflected component ops do, and answers
    //! through them, so the editor keeps addressing ECS state by (type, entity) alone.
    bool MaterialExists(uint32_t handleId);

    //! A material entity's asset identity as text. Three cases, all of them something the
    //! user needs told apart: the asset it came from, "(scene material)" for one no asset
    //! backs (the resident default today, scene-owned materials later), and "(none)" for a
    //! reference that resolves to nothing -- a deleted `.smat` lands there, and the object
    //! is quietly rendering with the default material.
    eastl::string MaterialIdentity(uint32_t handleId, bool exists);
}
