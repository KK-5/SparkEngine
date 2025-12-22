#pragma once

#include <Format.h>

#include "DX12.h"

namespace Spark::RHI::DX12
{
    DXGI_FORMAT ConvertFormat(RHI::Format format, bool raiseAsserts = true);
}