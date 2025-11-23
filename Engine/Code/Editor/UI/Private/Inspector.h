#pragma once

#include <imgui.h>

#include <ECS/Entity.h>

namespace Spark
{
    class WorldContext;
}

namespace Editor
{
    class Inspector final
    {
    public:
        struct EntityNode
        {
            EntityNode(Spark::Entity entity, Spark::WorldContext& context);

            ~EntityNode()
            {
                if (m_isOpen)
                {
                    ImGui::TreePop();
                }
            }
            
            bool IsOpen() const
            {
                return m_isOpen;
            }
        
        private:
            bool m_isOpen = false;
        };

        void Draw(Spark::WorldContext& context);
    
    private:
        void DrawEntityMenu(Spark::Entity entity, Spark::WorldContext& context);
        void DrawTools(Spark::WorldContext& context);
    };
}