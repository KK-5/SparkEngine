#pragma once

#include <Object/IObjectFactory.h>

#include <DX12.h>
#include "Descriptor.h"

namespace Spark::RHI::DX12
{
    struct DescriptorFactoryDesc : public Spark::IObjectFactory<DescriptorHandle>::Descriptor
    {
        ID3D12DeviceX* device;
    };

    //template <>
    class IObjectFactory<DescriptorHandle>
    {
    public:
        

    };
}