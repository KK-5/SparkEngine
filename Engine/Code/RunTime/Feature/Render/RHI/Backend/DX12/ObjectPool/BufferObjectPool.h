#pragma once

#include <RHI/Device/DeviceObjectFactory.h>

#include <Resource/Buffer/Buffer.h>

namespace Spark::RHI::DX12
{
    using BufferObjectPool = RHI::DeviceObjectFactory<Buffer>;

    
}