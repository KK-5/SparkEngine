#pragma once

#include <EASTL/functional.h>
#include <EASTL/unordered_set.h>
#include <EASTL/set.h>
#include <EASTL/queue.h>

#include <ECS/ISystem.h>
#include <ECS/Bus/ComponentEventBus.h>
#include <ECS/WorldContext.h>

#include "IScene.h"
#include "EntityHierarchy.h"
#include "Component/HierarchyComponent.h"

namespace Spark
{
    class SceneManager final : public ISystem,
                               public Service<IScene>::Handler,
                               public ComponentEventBus::Handler
    {
    public:
        SceneManager(WorldContext& context) : m_context(context) {}

        // ISystem
        void Initialize() override;
        void ShutDown() override;
        eastl::vector<HashString> Request() const override
        {
            return {"LogSystem"_hs};
        }

        HashString GetName() const override
        {
            return "SceneManager"_hs;
        }

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
        eastl::vector<Entity> GetRootEntities(eastl::function<bool(Entity, Entity)> compare) const override;
        eastl::vector<Entity> GetChildren(Entity entity) const override;
        size_t GetDepth(Entity entity) const override;
        eastl::vector<eastl::pair<Entity, unsigned int>> GetEntityTree() const override;
        void SetParent(Entity entity, Entity parent, Entity prevSibling = NullEntity) override;
        void PatchEntityHierarchy(Entity entity, eastl::function<void(Entity)> func) override;

        // ComponentEventBus
        void OnComponentConstruct(WorldContext& context, Entity entity) override;
        void OnComponentUpdate(WorldContext& context, Entity entity) override;
        void OnComponentDestory(WorldContext& context, Entity entity) override;
    
    private:
        /// @brief Update m_entityDFSTree according to m_roots and m_childrenMap
        void UpdateEntityTree();

        /// @brief Update m_childrenMap according to Hierarchy component of entity
        /// @param entity 
        void UpdateChildrenMap(Entity entity);

        /// @brief Update m_roots according to Hierarchy component of entity
        void UpdateRoots(Entity entity);

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

        eastl::queue<eastl::function<void()>> m_updateFunctions;

        WorldContext& m_context;

        // 缓存信息
        eastl::vector<eastl::pair<Entity, uint32_t>> m_entityDFSTree;
        eastl::unordered_set<Entity>  m_entities;
        eastl::unordered_set<Entity>  m_roots;
        eastl::unordered_map<Entity, eastl::vector<Entity>> m_childrenMap;
        eastl::unordered_map<Entity, Hierarchy> m_componentCache;
    };
}