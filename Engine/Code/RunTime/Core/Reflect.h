#pragma once

#include "Reflection/ReflectContext.h"
#include "ECS/NameComponent.h"
#include "HashString/HashString.h"

namespace Spark
{
    void RunTimeCoreReflect(ReflectContext& context)
    {
        context.Reflect<Name>().Type("Name"_hs, "Name")
            .Data<&Name::m_name>("m_name"_hs, "m_name");
    }
}