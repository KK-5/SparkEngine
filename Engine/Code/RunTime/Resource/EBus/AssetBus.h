#pragma once

#include <EBus/EBus.h>

namespace Spark::Resource
{
    class AssetType;

    struct AssetBusTraits : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = AssetType;

        

    };
    
}