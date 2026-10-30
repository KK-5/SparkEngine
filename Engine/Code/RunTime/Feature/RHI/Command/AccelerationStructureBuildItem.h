#pragma once

#include <RHI/RayTracing/AccelerationStructureBuildInputs.h>

namespace Spark::RHI
{
    enum class AccelerationStructureBuildMode : uint8_t
    {
        Build = 0,
        //! Not yet supported.
        Update
    };

    //! One build. Its barriers are the caller's, as any item's: inputs at
    //! AccelStructBuildInputRead, the destination's buffer at AccelStructWrite and the scratch at
    //! AccelStructScratch, all in the AccelerationStructureBuild stage.
    struct AccelerationStructureBuildItem
    {
        //! The geometries it spans must live until the item is submitted.
        AccelerationStructureBuildInputs m_inputs;
        AccelerationStructureBuildMode   m_mode          = AccelerationStructureBuildMode::Build;
        const AccelerationStructure*     m_destination   = nullptr;
        //! Update only.
        const AccelerationStructure*     m_source        = nullptr;
        const Buffer*                    m_scratchBuffer = nullptr;
        uint64_t                         m_scratchOffset = 0;
    };

    bool ValidateAccelerationStructureBuildItem(const Device& device, const AccelerationStructureBuildItem& item);
}
