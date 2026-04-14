#pragma once

#include <imgui.h>

#include <ECS/Common.h>

namespace Editor
{
    class Inspector final
    {
    public:
        struct EntityNode
        {
            EntityNode(Spark::Entity entity);

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

        void Draw();
    
    private:
        void DrawEntityMenu(Spark::Entity entity);
        void DrawTools();
    };
}