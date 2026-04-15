#pragma once

#include <RHI/Format.h>

namespace Spark::RHI
{
    struct ImGuiDescriptor
    {
        RHI::Format m_rtvFormat = RHI::Format::R8G8B8A8_UNORM;
        RHI::Format m_dsvFormat = RHI::Format::D32_FLOAT;
    };
    
}