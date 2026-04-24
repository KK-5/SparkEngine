#pragma once

#include <EASTL/functional.h>
#include <EASTL/unordered_set.h>
#include <EASTL/set.h>
#include <EASTL/queue.h>

#include <ECS/ISystem.h>
#include <ECS/Bus/ComponentEventBus.h>
#include <ECS/Common.h>

#include "IScene.h"
#include "Component/HierarchyComponent.h"

namespace Spark
{
    class SceneManager final : public ISystem,
                               public Service<IScene>::Handler,
                               public ComponentEventBus::Handler
    {
    public:
        SceneManager() = default;

        ///////////////////////////////////////////
        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;
        eastl::vector<HashString> Request() const override
        {
            return {"LogSystem"_hs};
        }

        HashString GetName() const override
        {
            return "SceneManager"_hs;
        }
        ///////////////////////////////////////////

        ///////////////////////////////////////////
        // IScene
        size_t GetEntityCount() const override;
        void AddEntity(Entity entity) override;
        void AddEntities(eastl::span<Entity> entities) override;
        void RemoveEntity(Entity entity) override;
        void RemoveEntities(eastl::span<Entity> entities) override;
        bool Contain(Entity entity) const override;
        eastl::vector<Entity> GetHierarchyPath(Entity entity) const override;
        bool IsAncestor(Entity entity, Entity ancestor) const override;
        Entity GetEntityRoot(Entity entity) const override;
        eastl::vector<Entity> GetRootEntities() const override;
        eastl::vector<Entity> GetChildren(Entity entity) const override;
        size_t GetDepth(Entity entity) const override;
        eastl::vector<eastl::pair<Entity, unsigned int>> GetEntityTree() const override;
        void SetParent(Entity entity, Entity parent, Entity prevSibling = NullEntity) override;
        void PatchEntityHierarchy(Entity entity, eastl::function<void(Entity)> func) override;
        ///////////////////////////////////////////

        ///////////////////////////////////////////
        // ComponentEventBus
        void OnComponentConstruct(Entity entity) override;
        void OnComponentWillUpdate(Entity entity) override;
        void OnComponentUpdated(Entity entity) override;
        void OnComponentDestory(Entity entity) override;
        ///////////////////////////////////////////
    
    private:
        /// @brief Remove entity hierarchy from the hierarchies, the functon will not trigger any Hierarchy component update event
        ///        or update m_childrenMap and m_roots
        /// @param hierarchy The Hierarchy component of the entity, the param is not a entity, because the entity has been updated or destoryed 
        void RemoveEntityInternal(const Hierarchy& hierarchy);

        /// @brief Add entity hierarchy to the hierarchies, the functon will not trigger any Hierarchy component update event
        ///        or update m_childrenMap and m_roots
        /// @param entity 
        void AddEntityInternal(Entity entity);

        bool Valid(const Hierarchy& hierarchy) const;

        void ForEachChild(const Hierarchy& hierarchy, eastl::function<void(Entity)> func);
    };
}