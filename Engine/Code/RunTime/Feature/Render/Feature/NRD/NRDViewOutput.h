#pragma once

#include <cstdint>

#include <EASTL/array.h>
#include <RHI/Attachment/AttachmentEnums.h>

namespace Spark::Render
{
    namespace NRDOutputs
    {
        inline const RHI::AttachmentId Diffuse("NRDDiffuseOutput");
        inline const RHI::AttachmentId Specular("NRDSpecularOutput");
    }

    //! Published on the main view for frames that declare NRD outputs.
    struct NRDViewOutput
    {
        eastl::array<uint32_t, 2> m_rectSize;
        float m_denoisingRange;
    };
}
