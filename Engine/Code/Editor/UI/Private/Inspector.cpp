#include "Inspector.h"

#include <imgui.h>
#include <EASTL/stack.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <CoreComponents/Name.h>
#include <Service/Service.h>
#include <Hierarchy/HierarchyComponent.h>
#include <Hierarchy/IHierarchy.h>
#include <Log/ILogSystem.h>
#include <CoreComponents/Tags.h>
#include "../../Component/Tags.h"

#include "EditorTheme.h"

namespace Editor
{
    using namespace Spark;

    Inspector::EntityNode::EntityNode(Entity entity)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | 
        ImGuiTreeNodeFlags_DrawLinesFull |
        ImGuiTreeNodeFlags_FramePadding |
        ImGuiTreeNodeFlags_SpanAllColumns |
        ImGuiTreeNodeFlags_DefaultOpen;

        auto& context = *WorldExecuteContext::Current();
        Hierarchy hierarchy = context.Get<Hierarchy>(entity);
        if (hierarchy.firstChild == NullEntity)
        {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }

        if (context.Has<SelectTag>(entity))
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        eastl::string name;
        if (context.Has<Name>(entity))
        {
            name = context.Get<Name>(entity).name;
        }
        else
        {
            // [TODO] std::fotmat will be better
            name = eastl::string("Entity[") + eastl::to_string(uint32_t(entity)) + "]";
        }

        if (context.Has<Renaming>(entity))
        {
            // rename
            m_isOpen = ImGui::TreeNodeEx("##RenamingNode", flags);
            ImGui::SameLine();
            float indent = ImGui::GetTreeNodeToLabelSpacing();
            float width = ImGui::GetContentRegionAvail().x - indent;
            ImGui::SetNextItemWidth(width);
            ImGui::SetKeyboardFocusHere();

            eastl::string newName;
            newName.resize(256);
            ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue | 
                                    ImGuiInputTextFlags_AutoSelectAll;
            if (ImGui::InputText("##RenameInput", newName.data(), 256, input_flags))
            {
                context.AddOrReplace<Name>(entity, newName);
                context.Remove<Renaming>(entity);
            }

            if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
            {
                context.AddOrReplace<Name>(entity, newName);
                context.Remove<Renaming>(entity);
            }
        }
        else
        {
            eastl::string dispalyName = name + "##" + eastl::to_string(uint32_t(entity));
            m_isOpen = ImGui::TreeNodeEx(dispalyName.c_str(), flags);
        }

        // click entity
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            if (!context.Has<SelectTag>(entity))
            {
                // 清除其他选择
                auto view = context.GetView<SelectTag>();
                for (Entity ent: view)
                {
                    context.Remove<SelectTag>(ent);
                }

                context.Add<SelectTag>(entity);
            }
            else
            {
                context.Remove<SelectTag>(entity);
            }

            if (!context.Has<ActiveTag>(entity))
            {
                auto view = context.GetView<ActiveTag>();
                if (view.size() > 1)
                {
                    LOG_ERROR("[Inspector] Thera are too many active entities in scene");
                }
                for (Entity ent: view)
                {
                    context.Remove<ActiveTag>(ent);
                }
                context.Add<ActiveTag>(entity);
            }
        }   
    }

    void Inspector::DrawEntityMenu(Entity entity)
    {
        auto hierarchy = Spark::Service<IHierarchy>::Get();
        auto& context = *WorldExecuteContext::Current();
        ImGui::TextDisabled("Entity");
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::MenuItem("Create Sub Entity"))
        {
            Entity subEntity = context.CreateEntity();
            hierarchy->SetParent(subEntity, entity);
        }
        ImGui::Spacing();
        if (ImGui::MenuItem("Copy Entity"))
        {

        }
        ImGui::Spacing();
        if (ImGui::MenuItem("Paste"))
        {

        }
        ImGui::Spacing();
        ImGui::Separator();
        
        if (ImGui::MenuItem("Select"))
        {
            if (!context.Has<SelectTag>(entity))
            {
                // 清除其他选择
                auto view = context.GetView<SelectTag>();
                for (Entity ent: view)
                {
                    context.Remove<SelectTag>(ent);
                }
                context.Add<SelectTag>(entity);
            }
        }
        ImGui::Spacing();
        if (ImGui::MenuItem("Deselect"))
        {
            if (context.Has<SelectTag>(entity))
            {
                context.Remove<SelectTag>(entity);
            }

        }
        ImGui::Spacing();
        if (ImGui::MenuItem("Select Hierarchy"))
        {
            auto view = context.GetView<SelectTag>();
            for (Entity ent: view)
            {
                context.Remove<SelectTag>(ent);
            }
            hierarchy->PatchEntityHierarchy(entity, [&](Entity ent){
                if (!context.Has<SelectTag>(ent))
                {
                    context.Add<SelectTag>(ent);
                }
            });
        }
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::MenuItem("Delete"))
        {
            auto view = context.GetView<SelectTag>();
            for (Entity ent: view)
            {
                if (!context.Has<DeadTag>(ent))
                {
                    context.Add<DeadTag>(ent);
                }
            }
        }
        ImGui::Spacing();
        if (ImGui::MenuItem("Delete Hierarchy"))
        {
            hierarchy->PatchEntityHierarchy(entity, [&](Entity ent){
                if (!context.Has<DeadTag>(ent))
                {
                    context.Add<DeadTag>(ent);
                }
            });
        }
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::MenuItem("Rename")) {
            if (!context.Has<Renaming>(entity))
            {
                context.Add<Renaming>(entity);
            }
        }
    }

    void Inspector::DrawTools()
    {
        auto& context = *WorldExecuteContext::Current();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kButtonHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kButtonHov);
        if (ImGui::Button(" + "))
        {
            Entity entity = context.CreateEntity();
            Spark::Service<IHierarchy>::Get()->AddEntity(entity);
            eastl::string name = "Entity[" + eastl::to_string(uint32_t(entity)) + "]";
            context.AddOrReplace<Name>(entity, name);
        }
        ImGui::PopStyleColor(3);
    }
    

    void Inspector::Draw()
    {
        if (!Spark::Service<IHierarchy>::Get())
        {
            LOG_ERROR("[Inspector] Draw: IHierarchy is null!");
            return;
        }

        IHierarchy* hierarchy = Spark::Service<IHierarchy>::Get();
        auto& context = *WorldExecuteContext::Current();

        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar);
        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        float toolHeight = Theme::Px(28.f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kFooterBg);
        ImGui::BeginChild("InspectorTools", ImVec2(windowSize.x, toolHeight), false, ImGuiWindowFlags_NoTitleBar);
        DrawTools();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        float height = windowSize.y - toolHeight;
        float rowHeight = Theme::Px(22.f);
        uint32_t rowTotalCount = uint32_t(height / rowHeight) + 1;
        uint32_t curRowCount = 0;

        if (ImGui::BeginTable("EntityList", 1, 
            ImGuiTableFlags_RowBg
            ))
        {
            ImGui::TableSetupColumn("Entities", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHeaderWidth);
            auto DefaultSort = [&context](Entity ent1, Entity ent2)
            {
                return (uint32_t)ent1 < (uint32_t) ent2;
            };

            auto SortByName = [&context, &DefaultSort](Entity ent1, Entity ent2)
            {
                if (!context.Has<Name>(ent1) || !context.Has<Name>(ent2))
                {
                    return DefaultSort(ent1, ent2);
                }

                return context.Get<Name>(ent1).name < context.Get<Name>(ent2).name;
            };

            eastl::vector<Entity> roots = hierarchy->GetRootEntities();
            eastl::stack<eastl::pair<Entity, int32_t>> stack;
            // eastl::stack底层默认用vector，用unique_ptr方式容器扩容时意外析构
            eastl::stack<eastl::unique_ptr<EntityNode>> nodeStack;
            for (const auto& root: roots)
            {
                stack.emplace(root, 0);
                while(!stack.empty())
                {
                    Entity cur = stack.top().first;
                    int32_t curDepth = stack.top().second;
                    stack.pop();

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                    curRowCount++;

                    ImGui::TableSetColumnIndex(0);
                    nodeStack.emplace(eastl::make_unique<EntityNode>(cur));
                    if (nodeStack.top()->IsOpen())
                    {
                        eastl::vector<Entity> children = hierarchy->GetChildren(cur);
                        for (auto it = children.rbegin(); it != children.rend(); ++it)
                        {
                            stack.emplace(*it, curDepth + 1);
                        }
                    }
                    
                    // 检查下一个需要处理的节点深度，如果下一个节点深度小于或等于当前节点深度，需要执行pop直到深度回退到下一节点之上
                    int32_t nextDepth = stack.empty() ? 0 : stack.top().second;
                    // 注意curDepth不能用无符号数，因为nextDepth可能是0
                    while(curDepth >= nextDepth)
                    {
                        nodeStack.pop();
                        curDepth--;
                    }

                    // item menu
                    if (ImGui::BeginPopupContextItem())
                    {
                        DrawEntityMenu(cur);
                        ImGui::EndPopup();
                    }
                }
            }

            while(curRowCount < rowTotalCount)
            {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                curRowCount++;
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }
}