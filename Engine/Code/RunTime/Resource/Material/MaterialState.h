#pragma once

#include <cstdint>

namespace Spark::Resource
{
    //! Frozen on disk: `state`'s "Alpha Mode" is written as one of these names. Append
    //! only -- glTF's three today, and a richer set (additive, modulate, premultiplied)
    //! is a set of new values rather than a new key when a translucent path exists.
    enum class AlphaMode : uint8_t
    {
        Opaque,
        Mask,
        Blend,
    };

    //! A material's `state`: the values that pick a pipeline state or a render path,
    //! kept apart from the shading properties because the consumers differ -- these
    //! reach the PSO, the properties reach the material constant buffer and the texture
    //! bindings. Moving a key between the two later would break the format, so the split
    //! is made once, here.
    //!
    //! The line is "does the material's author state this", not "is it a PSO field": fill
    //! mode, depth bias, depth test and MSAA are the pass's to decide, and blending and
    //! depth writes follow from m_alphaMode rather than being authored separately. What
    //! remains is exactly what a glTF material authors.
    struct MaterialState
    {
        AlphaMode m_alphaMode   = AlphaMode::Opaque;
        float     m_alphaCutoff = 0.5f;      // only meaningful when m_alphaMode == Mask

        //! glTF's `doubleSided`. Authored as a fact about the surface, not as a cull mode:
        //! which face gets culled is the renderer's convention, and RHI::CullMode's
        //! enumerator names would become file format if they were stored here.
        bool      m_doubleSided = false;
    };
}
