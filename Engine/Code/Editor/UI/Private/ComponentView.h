#pragma once
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>

#include <ECS/Entity.h>
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
        struct ComponentState
        {
            ComponentState(eastl::string_view _name, bool _isExpanded): name(_name), isExpanded(_isExpanded)
            {}

            eastl::string name;
            bool isExpanded;
        };

        void DrawElement(Spark::WorldContext& context, Spark::MetaData& data, Spark::MetaAny& instance, float width);
        void DrawComponent(Spark::WorldContext& context, const Spark::MetaType component, Spark::MetaAny& instance);

        eastl::unordered_map<Spark::TypeId, ComponentState> m_componentState;
        Spark::Entity m_activeEntity;
    };
}