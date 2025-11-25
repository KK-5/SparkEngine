#pragma once

#include <Reflection/RTTI.h>

namespace Spark
{
    class WorldContext;
}

namespace Editor
{
    class ComponentView final
    {
    public:
        void Draw(Spark::WorldContext& context);

    private:
        void DrawComponent();
        void DrawElement(Spark::MetaAny& data, const Spark::MetaData& field, const Spark::MetaCustom& uiElement);
    };
}