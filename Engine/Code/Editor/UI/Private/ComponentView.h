#pragma once
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>

#include <ECS/Common.h>
#include <Reflection/RTTI.h>

namespace Editor
{
    class ComponentView final
    {
    public:
        void Draw();

    private:
        struct ComponentState
        {
            ComponentState(eastl::string_view _name, bool _isExpanded): name(_name), isExpanded(_isExpanded)
            {}

            eastl::string name;
            bool isExpanded;
        };

        void DrawElement(Spark::MetaData& data, Spark::MetaAny& instance, float width);
        void DrawComponent(const Spark::MetaType component, Spark::MetaAny& instance);

        eastl::unordered_map<Spark::TypeId, ComponentState> m_componentState;
        Spark::Entity m_activeEntity;
    };
}