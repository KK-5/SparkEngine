#include "SceneManager.h"

#include <EASTL/stack.h>
#include <EASTL/sort.h>

#include <Log/SpdLogSystem.h>
#include <ECS/Tag.h>
#include <Reflection/RTTI.h>

namespace Spark
{
    void SceneManager::Initialize()
    {
        m_context.SetupComponentEvents<Hierarchy>();
        ComponentEventBus::Handler::BusConnect(GetTypeId<Hierarchy>());
    }

    void SceneManager::ShutDown()
    {
        m_roots.clear();
        m_childrenMap.clear();
        m_entities.clear();
        m_componentCache.clear();
        m_entityDFSTree.clear();

        ComponentEventBus::Handler::BusDisconnect(GetTypeId<Hierarchy>());
    }

    size_t SceneManager::GetEntityCount() const
    {
        return m_entities.size();
    }

    void SceneManager::AddEntity(Entity entity)
    {
        if (entity == NullEntity)
        {
            LOG_ERROR("[SceneManager] AddEntity: entity is null");
            return;
        }

        if (!m_context.Has<Hierarchy>(entity))
        {
            m_context.Add<Hierarchy>(entity);
        }
    }

    void SceneManager::AddEntities(eastl::span<Entity> entities)
    {
        for (const auto ent: entities)
        {
            if (ent == NullEntity)
            {
                LOG_ERROR("[SceneManager] AddEntities: Thera is a null entity in input entities");
                return;
            }
        }

        m_context.Add<Hierarchy>(entities.begin(), entities.end(), Hierarchy{});
    }

    void SceneManager::RemoveEntity(Entity entity)
    {
        if (entity == NullEntity)
        {
            LOG_ERROR("[SceneManager] RemoveEntity: entity is null");
            return;
        }

        if (m_context.Has<Hierarchy>(entity))
        {
            m_context.Remove<Hierarchy>(entity);
        }
    }

    void SceneManager::RemoveEntities(eastl::span<Entity> entities)
    {
        for (const auto ent: entities)
        {
            if (ent == NullEntity)
            {
                LOG_ERROR("[SceneManager] RemoveEntities: Thera is a null entity in input entities");
                return;
            }
        }

        m_context.Remove<Hierarchy>(entities.begin(), entities.end());
    }

    bool SceneManager::Contain(Entity entity) const
    {
        if (entity == NullEntity)
        {
            LOG_ERROR("[SceneManager] Contain: entity is null");
            return false;
        }

        return m_entities.contains(entity);
    }

    eastl::vector<Entity> SceneManager::GetHierarchyPath(Entity entity) const
    {
        eastl::vector<Entity> ancestors;

        Entity cur = entity;
        while(m_context.Has<Hierarchy>(cur))
        {
            Entity parent = m_context.Get<Hierarchy>(cur).parent;
            if (parent == NullEntity)
            {
                break;
            }
            ancestors.push_back(parent);
            cur = parent;
        }

        eastl::reverse(ancestors.begin(), ancestors.end());
        return ancestors;
    }

    bool SceneManager::IsAncestor(Entity entity, Entity ancestor) const
    {
        Entity cur = entity;
        while(m_context.Has<Hierarchy>(cur))
        {
            Entity parent = m_context.Get<Hierarchy>(cur).parent;
            if (parent == NullEntity)
            {
                break;
            }

            if (ancestor == parent)
            {
                return true;
            }
            cur = parent;
        }
        return false;
    }

    Entity SceneManager::GetEntityRoot(Entity entity) const
    {
        Entity cur = entity;
        while(m_context.Has<Hierarchy>(cur))
        {
            Entity parent = m_context.Get<Hierarchy>(cur).parent;
            if (parent == NullEntity)
            {
                break;
            }

            cur = parent;
        }

        return cur;
    }

    eastl::vector<Entity> SceneManager::GetRootEntities() const
    {
        return eastl::vector<Entity>(m_roots.begin(), m_roots.end());
    }

    eastl::vector<Entity> SceneManager::GetRootEntities(eastl::function<bool(Entity, Entity)> compare) const
    {
        eastl::vector result = GetRootEntities();
        eastl::sort(result.begin(), result.end(), compare);
        return result;
    }

    eastl::vector<Entity> SceneManager::GetChildren(Entity entity) const
    {
        eastl::vector<Entity> children;
        auto it = m_childrenMap.find(entity);
        if (it != m_childrenMap.end())
        {
            children = it->second;
        }
        return children;
    }

    size_t SceneManager::GetDepth(Entity entity) const
    {
        size_t depth = 0;

        Entity cur = entity;
        while(m_context.Has<Hierarchy>(cur))
        {
            Entity parent = m_context.Get<Hierarchy>(cur).parent;
            if (parent == NullEntity)
            {
                break;
            }
            depth++;
            cur = parent;
        }

        return depth;
    }

    eastl::vector<eastl::pair<Entity, unsigned int>> SceneManager::GetEntityTree() const
    {
        return m_entityDFSTree;
    }

    void SceneManager::UpdateEntityTree()
    {
        eastl::vector<Entity> roots = GetRootEntities();

        m_entityDFSTree.clear();
        m_entityDFSTree.reserve(m_entities.size());
        for (auto root: roots)
        {
            eastl::stack<eastl::pair<Entity, uint32_t>> traversalStack;
            traversalStack.emplace(root, 0);
            while (!traversalStack.empty())
            {
                auto cur = traversalStack.top();
                traversalStack.pop();

                m_entityDFSTree.emplace_back(cur);

                eastl::vector<Entity> children = GetChildren(cur.first);
                uint32_t depth = cur.second + 1;
                for (auto it = children.rbegin(); it != children.rend(); it++)
                {
                    traversalStack.emplace(eastl::make_pair<Entity, uint32_t>(*it, depth));
                }
            }
        }

        if (m_entityDFSTree.size() != m_entities.size())
        {
            LOG_ERROR("[SceneManager] UpdateEntityTree: An error has occurred in entity hierarchy.");
        }
    }

    void SceneManager::SetParent(Entity entity, Entity parent, Entity prevSibling)
    {
        if (entity == NullEntity || parent == NullEntity)
        {
            LOG_ERROR("[SceneManager] SetParent: entity or parent is null");
            return;
        }

        if (parent != NullEntity && !Contain(parent))
        {
            AddEntity(parent);
        }
        if (prevSibling != NullEntity && !Contain(prevSibling))
        {
            AddEntity(prevSibling);
        }

        Hierarchy entityHier;
        if (m_context.Has<Hierarchy>(entity))
        {
            entityHier = m_context.Get<Hierarchy>(entity);
        }

        // get next
        Entity next = NullEntity;
        if (prevSibling != NullEntity)
        {
            next = m_context.Get<Hierarchy>(prevSibling).nextSibling;
        }
        else
        {
            next = m_context.Get<Hierarchy>(parent).firstChild;
        }

        entityHier.parent = parent;
        entityHier.prevSibling = prevSibling;
        entityHier.nextSibling = next;
        m_context.AddOrRepalce<Hierarchy>(entity, entityHier);
    }

    void SceneManager::PatchEntityHierarchy(Entity entity, eastl::function<void(Entity)> func)
    {
        eastl::stack<Entity> traversalStack;
        traversalStack.emplace(entity);
        while (!traversalStack.empty())
        {
            auto cur = traversalStack.top();
            traversalStack.pop();

            func(cur);

            eastl::vector<Entity> children = GetChildren(cur);
            for (auto it = children.rbegin(); it != children.rend(); it++)
            {
                traversalStack.emplace(*it);
            }
        }
    }

    void SceneManager::UpdateChildrenMap(Entity entity)
    {
        if (entity == NullEntity)
        {
            LOG_ERROR("[SceneManager] UpdateChildrenMap: NullEntity does not have children map");
            return;
        }

        if (!m_context.Has<Hierarchy>(entity))
        {
            LOG_ERROR("[SceneManager] UpdateChildrenMap: The entity does not have Hierarchy component");
            return;
        }

        const auto hierarchy = m_context.Get<Hierarchy>(entity);
        eastl::vector<Entity> newChildren;
        Entity cur = hierarchy.firstChild;
        while(cur != NullEntity)
        {
            newChildren.push_back(cur);
            cur = m_context.Has<Hierarchy>(cur) ? m_context.Get<Hierarchy>(cur).nextSibling : NullEntity;
        }

        if (newChildren.empty())
        {
            m_childrenMap.erase(entity);
            return;
        }

        auto it = m_childrenMap.find(entity);
        if (it != m_childrenMap.end())
        {
            newChildren.swap(it->second);
        }
        else
        {
            m_childrenMap[entity] = newChildren;
        }
    }

    void SceneManager::UpdateRoots(Entity entity)
    {
        if (entity == NullEntity)
        {
            LOG_ERROR("[SceneManager] UpdateRoots: entity is null");
            return;
        }

        if (!m_context.Has<Hierarchy>(entity))
        {
            LOG_ERROR("[SceneManager] UpdateRoots: The entity does not have Hierarchy component");
            return;
        }

        if (m_context.Get<Hierarchy>(entity).parent != NullEntity)
        {
            m_roots.erase(entity);
        }
        else
        {
            m_roots.insert(entity);
        }
    }

    bool SceneManager::Valid(const Hierarchy& hierarchy) const
    {
        if (hierarchy.parent != NullEntity && !Contain(hierarchy.parent))
        {
            LOG_ERROR("[SceneManager] Valid: Entity has parent but the parent entity is not in scene.");
            return false;
        }

        if (hierarchy.prevSibling != NullEntity && !Contain(hierarchy.prevSibling))
        {
            LOG_ERROR("[SceneManager] Valid: Entity has prevSibling but the prevSibling entity is not in scene.");
            return false;
        }

        if (hierarchy.nextSibling != NullEntity && !Contain(hierarchy.nextSibling))
        {
            LOG_ERROR("[SceneManager] Valid: Entity has nextSibling but the nextSibling entity is not in scene.");
            return false;
        }

        if (hierarchy.firstChild != NullEntity && !Contain(hierarchy.firstChild))
        {
            LOG_ERROR("[SceneManager] Valid: Entity has firstChild but the firstChild entity is not in scene.");
            return false;
        }

        if (hierarchy.nextSibling != NullEntity || hierarchy.prevSibling != NullEntity)
        { 
            if (hierarchy.parent == NullEntity)
            {
                LOG_ERROR("[SceneManager] Valid: Entity has sibling but it does not has a parent.");
                return false;
            }

            if (hierarchy.prevSibling != NullEntity)
            {
                Entity siblingParent = m_context.Get<Hierarchy>(hierarchy.prevSibling).parent;
                if (siblingParent != hierarchy.parent)
                {
                    LOG_ERROR("[SceneManager] Valid: Entity and its previous sibling has a different parent.");
                    return false;
                }
                Entity next = m_context.Get<Hierarchy>(hierarchy.prevSibling).nextSibling;
                if (next != hierarchy.nextSibling)
                {
                    LOG_ERROR("[SceneManager] Valid: The previous sibling has a next entity and it is different from the nextSibling in this Hierarchy.");
                    return false;
                }
            }

            if (hierarchy.nextSibling != NullEntity)
            {
                Entity siblingParent = m_context.Get<Hierarchy>(hierarchy.nextSibling).parent;
                if (siblingParent != hierarchy.parent)
                {
                    LOG_ERROR("[SceneManager] Valid: Entity and its next sibling has a different parent.");
                    return false;
                }
                Entity prev = m_context.Get<Hierarchy>(hierarchy.nextSibling).prevSibling;
                if (prev != hierarchy.prevSibling)
                {
                    LOG_ERROR("[SceneManager] Valid: The next sibling has a previous entity and it is different from the prevSibling in this Hierarchy.");
                    return false;
                }
            }

            if (hierarchy.prevSibling != NullEntity && hierarchy.nextSibling != NullEntity)
            {
                Entity next = m_context.Get<Hierarchy>(hierarchy.prevSibling).nextSibling;
                Entity prev = m_context.Get<Hierarchy>(hierarchy.nextSibling).prevSibling;
                if (next != prev)
                {
                    LOG_ERROR("[SceneManager] Valid: Hierarchy both set prevSibling and nextSibling but they are not adjacent now.");
                    return false;
                }
            }
        }
        else  // both prev and next are null
        {
            if (hierarchy.parent != NullEntity)
            {
                if (m_context.Get<Hierarchy>(hierarchy.parent).firstChild != NullEntity)
                {
                    LOG_ERROR("[SceneManager] Valid: The parent entity already has child,"
                        "but Hierarchy have not specified the insertion position for this entity."
                        "(Both prevSibling and nextSibling are null)");
                    return false;
                }
            }
        }

        return true;
    }

    void SceneManager::ForEachChild(const Hierarchy& hierarchy, eastl::function<void(Entity entity)> func)
    {
        Entity cur = hierarchy.firstChild;
        while(cur != NullEntity && m_context.Has<Hierarchy>(cur))
        {
            func(cur);
            cur = m_context.Get<Hierarchy>(cur).nextSibling;
        }
    }

    void SceneManager::RemoveEntityInternal(const Hierarchy& hierarchy)
    {
        Entity parent = hierarchy.parent;
        Entity prevSibling = hierarchy.prevSibling;
        Entity nextSibling = hierarchy.nextSibling;
        Entity firstChild = hierarchy.firstChild;

        if (parent != NullEntity)
        {
            if (prevSibling == NullEntity)
            {
                auto& parentHier = m_context.Get<Hierarchy>(parent);
                parentHier.firstChild = nextSibling;
                m_componentCache[parent] = parentHier;
            }
            m_updateFunctions.emplace([this, parent](){UpdateChildrenMap(parent);});
        }

        // 子节点上升至父节点的父节点
        Entity cur = firstChild;
        Entity first = cur;
        Entity last = cur;
        bool isFirst = true;
        ForEachChild(hierarchy, [&](Entity child){
            auto& curHier = m_context.Get<Hierarchy>(child);
            if (isFirst && parent != NullEntity)
            {
                auto& parentHier =  m_context.Get<Hierarchy>(parent);
                if (parentHier.firstChild == NullEntity)
                {
                    parentHier.firstChild = child;
                }
                isFirst = false;
            }
            curHier.parent = parent;
            m_componentCache[child] = curHier;
            m_updateFunctions.emplace([this, child](){UpdateRoots(child);});
            last = child;
        });

        if (prevSibling != NullEntity)
        {
            auto& prevSiblingHier = m_context.Get<Hierarchy>(prevSibling);
            if (first != NullEntity)
            {
                auto& firstHier = m_context.Get<Hierarchy>(first);
                firstHier.prevSibling = prevSibling;
                prevSiblingHier.nextSibling = first;
                m_componentCache[first] = firstHier;
            }
            else
            {
                prevSiblingHier.nextSibling = nextSibling;
            }
            m_componentCache[prevSibling] = prevSiblingHier;
        }

        if (nextSibling != NullEntity)
        {
            auto& nextSiblingHier = m_context.Get<Hierarchy>(nextSibling);
            if (last != NullEntity)
            {
                auto& lastHier = m_context.Get<Hierarchy>(last);
                lastHier.nextSibling = nextSibling;
                nextSiblingHier.prevSibling = last;
                m_componentCache[last] = lastHier;
            }
            else
            {
                nextSiblingHier.prevSibling = prevSibling;
            }
            m_componentCache[nextSibling] = nextSiblingHier;
        }
    }

    void SceneManager::AddEntityInternal(Entity entity)
    {
        const auto hier = m_context.Get<Hierarchy>(entity);

        Entity parent = hier.parent;
        Entity prevSibling = hier.prevSibling;
        Entity nextSibling = hier.nextSibling;
        Entity firstChild = hier.firstChild;

        if (parent != NullEntity)
        {
            // 插入至第一个子节点
            if (prevSibling == NullEntity)
            {
                auto& parentHier = m_context.Get<Hierarchy>(parent);
                parentHier.firstChild = entity;
                m_componentCache[parent] = parentHier;
            }
            m_updateFunctions.emplace([this, parent](){ 
                UpdateChildrenMap(parent);
            });
        }

        if (prevSibling == NullEntity && nextSibling != NullEntity)
        {
            prevSibling = m_context.Get<Hierarchy>(nextSibling).prevSibling;
        }

        if (prevSibling != NullEntity && nextSibling == NullEntity)
        {
            nextSibling = m_context.Get<Hierarchy>(prevSibling).nextSibling;
        }

        if (prevSibling != NullEntity)
        {
            auto& prevSiblingHier = m_context.Get<Hierarchy>(prevSibling);
            prevSiblingHier.nextSibling = entity;
            m_componentCache[prevSibling] = prevSiblingHier;
            m_updateFunctions.emplace([this, prevSibling](){UpdateRoots(prevSibling);});
        }

        if (nextSibling != NullEntity)
        {
            auto& nextSiblingHier = m_context.Get<Hierarchy>(nextSibling);
            nextSiblingHier.prevSibling = entity;
            m_componentCache[nextSibling] = nextSiblingHier;
            m_updateFunctions.emplace([this, nextSibling](){UpdateRoots(nextSibling);});
        }

        bool isFirst = true;
        ForEachChild(hier, [&](Entity child){
            auto& curHier = m_context.Get<Hierarchy>(child);
            curHier.parent = entity;
            if (isFirst)
            {
                Entity prev = curHier.prevSibling;
                if (prev != NullEntity)
                {
                    auto& hier = m_context.Get<Hierarchy>(prev);
                    hier.nextSibling = NullEntity;
                    Entity oldParent = hier.parent;
                    m_updateFunctions.emplace([this, oldParent](){UpdateChildrenMap(oldParent);});
                }
                curHier.prevSibling = NullEntity;
                m_updateFunctions.emplace([this, entity](){UpdateChildrenMap(entity);});
                isFirst = false;
            }
            m_componentCache[child] = curHier;
            m_updateFunctions.emplace([this, child](){UpdateRoots(child);});
        }); 

        m_updateFunctions.emplace([this, entity](){UpdateRoots(entity);});
    }

    void SceneManager::OnComponentConstruct(WorldContext& context, Entity entity)
    {
        const auto hier = context.Get<Hierarchy>(entity);
        if (!Valid(hier))
        {
            LOG_ERROR("[SceneManager] OnComponentConstruct: Hierarchy is invalid");
            return;
        }
        
        AddEntityInternal(entity);
        m_entities.insert(entity);
        m_componentCache[entity] = hier;

        while(!m_updateFunctions.empty())
        {
            auto func = m_updateFunctions.front();
            func();
            m_updateFunctions.pop();
        }
        UpdateEntityTree();
    }

    void SceneManager::OnComponentUpdate(WorldContext& context, Entity entity)
    {
        const auto hier = context.Get<Hierarchy>(entity);
        if (!Valid(hier))
        {
            LOG_ERROR("[SceneManager] OnComponentUpdate: Hierarchy is invalid");
            return;
        }

        if (!m_componentCache.contains(entity))
        {
            OnComponentConstruct(context, entity);
            return;
        }
        // process old hierarchy
        auto cacheHierarchy = m_componentCache[entity];
        RemoveEntityInternal(cacheHierarchy);

        while(!m_updateFunctions.empty())
        {
            auto func = m_updateFunctions.front();
            func();
            m_updateFunctions.pop();
        }

        // update hierarchy
        OnComponentConstruct(context, entity);
    }

    void SceneManager::OnComponentDestory(WorldContext& context, Entity entity)
    {
        if (!m_componentCache.contains(entity))
        {
            return;
        }

        const auto hier = context.Get<Hierarchy>(entity);
        RemoveEntityInternal(hier);
        
        while(!m_updateFunctions.empty())
        {
            auto func = m_updateFunctions.front();
            func();
            m_updateFunctions.pop();
        }
        
        m_componentCache.erase(entity);
        m_roots.erase(entity);
        m_entities.erase(entity);
        UpdateEntityTree();
    }
}