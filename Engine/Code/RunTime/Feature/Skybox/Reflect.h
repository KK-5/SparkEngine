#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include "Components.h"

namespace Spark::Skybox
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<SkyboxComponent>()
            .Type("Skybox").Custom<ComponentTraitsRuntime>(ComponentTraits<SkyboxComponent>{})
            .Data<&SkyboxComponent::m_imageAssetId>("Image Asset")
                .Custom<Spark::AssetElement>(false, static_cast<uint32_t>(Resource::AssetType::Image))
            .Data<&SkyboxComponent::m_intensity>("Intensity")
                .Custom<Spark::FloatElement>(0.f, 10.f, 0.01f)
            ;

        Spark::ComponentOpertion<SkyboxComponent>(context);
    }
}
