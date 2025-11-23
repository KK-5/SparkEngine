#pragma once

#include <EASTL/vector.h>
#include <EASTL/unordered_set.h>
#include <EASTL/utility.h>
#include <EASTL/span.h>
#include <EASTL/set.h>
#include <EASTL/functional.h>

#include <ECS/Entity.h>

namespace Spark
{
    /**
    *  查询场景中的Entity信息
    */
    class WorldContext;

    class IScene
    {
    public:
        virtual size_t GetEntityCount() const = 0;

        /// @brief Add an entity to the scene(Add a Hierarchy component to the entity)
        /// @param entity 
        virtual void AddEntity(Entity entity) = 0;

        virtual void AddEntities(eastl::span<Entity> entities) = 0;

        /// @brief Remove an entity from the scene (Remove Hierarchy component on the entity)
        /// @param entity 
        virtual void RemoveEntity(Entity entity) = 0;

        virtual void RemoveEntities(eastl::span<Entity> entities) = 0;

        virtual bool Contain(Entity entity) const = 0;

        virtual eastl::vector<Entity> GetHierarchyPath(Entity entity) const = 0;

        virtual bool IsAncestor(Entity entity, Entity ancestor) const = 0;

        virtual Entity GetEntityRoot(Entity entity) const = 0;

        virtual eastl::vector<Entity> GetRootEntities() const = 0;

        virtual eastl::vector<Entity> GetRootEntities(eastl::function<bool(Entity, Entity)> compare) const = 0;

        virtual eastl::vector<Entity> GetChildren(Entity entity) const = 0;

        virtual size_t GetDepth(Entity entity) const = 0;

        /// @brief Get DFS tree of all entities in scene
        /// @return pair.first -- entity
        ///         pair.second -- entity's depth
        virtual eastl::vector<eastl::pair<Entity, uint32_t>> GetEntityTree() const = 0;

        /// @brief Set an entity's parent, it will remove entity's old hierarchy relationship,
        ///  if prevSibling or parent does not have hierarchy, add hierarchy to it.
        /// @param entity 
        /// @param parent The new parent
        /// @param prevSibling The previous sibling of this entity, if it not be assigned, the entity will be set to the first child of the parent
        virtual void SetParent(Entity entity, Entity parent, Entity prevSibling = NullEntity) = 0;

        virtual void PatchEntityHierarchy(Entity entity, eastl::function<void(Entity)> func) = 0;

    };
}