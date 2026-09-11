#include "HierarchyManager.h"

#include <EASTL/stack.h>
#include <EASTL/sort.h>

#include <Log/ILogSystem.h>
#include <ECS/Tag.h>
#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <ECS/Merge/MergeComponents.h>
#include <Reflection/RTTI.h>

namespace Spark
{
    void HierarchyManager::InitInternal()
    {
        auto& context = *WorldExecuteContext::Current();
        context.RegisterEventOnEntityRemove<Hierarchy>();
        ComponentEventBus::Handler::BusConnect(GetTypeId<Hierarchy>());
    }

    void HierarchyManager::ShutdownInternal()
    {
        ComponentEventBus::Handler::BusDisconnect(GetTypeId<Hierarchy>());
    }

    size_t HierarchyManager::GetEntityCount() const
    {
        auto& context = *WorldExecuteContext::Current();
        auto view = context.GetView<Hierarchy>();
        return view.size();
    }

    void HierarchyManager::AddEntity(Entity entity)
    {
        auto& context = *WorldExecuteContext::Current();
        if (entity == NullEntity)
        {
            LOG_ERROR("[HierarchyManager] AddEntity: entity is null");
            return;
        }

        if (!context.Has<Hierarchy>(entity))
        {
            context.Add<Hierarchy>(entity);
        }
    }

    void HierarchyManager::AddEntities(eastl::span<Entity> entities)
    {
        for (const auto ent: entities)
        {
            if (ent == NullEntity)
            {
                LOG_ERROR("[HierarchyManager] AddEntities: Thera is a null entity in input entities");
                return;
            }
        }
        auto& context = *WorldExecuteContext::Current();
        context.Add<Hierarchy>(entities.begin(), entities.end(), Hierarchy{});
    }

    void HierarchyManager::RemoveEntity(Entity entity)
    {
        if (entity == NullEntity)
        {
            LOG_ERROR("[HierarchyManager] RemoveEntity: entity is null");
            return;
        }

        auto& context = *WorldExecuteContext::Current();
        if (context.Has<Hierarchy>(entity))
        {
            context.Remove<Hierarchy>(entity);
        }
    }

    void HierarchyManager::RemoveEntities(eastl::span<Entity> entities)
    {
        for (const auto ent: entities)
        {
            if (ent == NullEntity)
            {
                LOG_ERROR("[HierarchyManager] RemoveEntities: Thera is a null entity in input entities");
                return;
            }
        }

        auto& context = *WorldExecuteContext::Current();
        context.Remove<Hierarchy>(entities.begin(), entities.end());
    }

    bool HierarchyManager::Contain(Entity entity) const
    {
        if (entity == NullEntity)
        {
            LOG_ERROR("[HierarchyManager] Contain: entity is null");
            return false;
        }

        auto& context = *WorldExecuteContext::Current();
        return context.Has<Hierarchy>(entity);
    }

    eastl::vector<Entity> HierarchyManager::GetHierarchyPath(Entity entity) const
    {
        eastl::vector<Entity> ancestors;

        auto& context = *WorldExecuteContext::Current();
        Entity cur = entity;
        while(context.Has<Hierarchy>(cur))
        {
            Entity parent = context.Get<Hierarchy>(cur).parent;
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

    bool HierarchyManager::IsAncestor(Entity entity, Entity ancestor) const
    {
        auto& context = *WorldExecuteContext::Current();
        Entity cur = entity;
        while(context.Has<Hierarchy>(cur))
        {
            Entity parent = context.Get<Hierarchy>(cur).parent;
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

    Entity HierarchyManager::GetEntityRoot(Entity entity) const
    {
        auto& context = *WorldExecuteContext::Current();
        Entity cur = entity;
        while(context.Has<Hierarchy>(cur))
        {
            Entity parent = context.Get<Hierarchy>(cur).parent;
            if (parent == NullEntity)
            {
                break;
            }

            cur = parent;
        }

        return cur;
    }

    eastl::vector<Entity> HierarchyManager::GetRootEntities() const
    {
        auto& context = *WorldExecuteContext::Current();
        auto roots = context.GetView<HierarchyRootTag>();

        return eastl::vector<Entity>(roots.begin(), roots.end());
    }

    eastl::vector<Entity> HierarchyManager::GetChildren(Entity entity) const
    {
        auto& context = *WorldExecuteContext::Current();

        eastl::vector<Entity> children;
        if (entity == NullEntity || !context.Has<Hierarchy>(entity))
        {
            return children;
        }

        const auto& hierarchy = context.Get<Hierarchy>(entity);
        Entity cur = hierarchy.firstChild;
        while(cur != NullEntity)
        {
            children.push_back(cur);
            cur = context.Has<Hierarchy>(cur) ? context.Get<Hierarchy>(cur).nextSibling : NullEntity;
        }

        return children;
    }

    size_t HierarchyManager::GetDepth(Entity entity) const
    {
        size_t depth = 0;

        Entity cur = entity;
        auto& context = *WorldExecuteContext::Current();
        while(context.Has<Hierarchy>(cur))
        {
            Entity parent = context.Get<Hierarchy>(cur).parent;
            if (parent == NullEntity)
            {
                break;
            }
            depth++;
            cur = parent;
        }

        return depth;
    }

    eastl::vector<eastl::pair<Entity, unsigned int>> HierarchyManager::GetEntityTree() const
    {
        eastl::vector<eastl::pair<Entity, unsigned int>> result;
        eastl::vector<Entity> roots = GetRootEntities();
        size_t count = GetEntityCount();

        result.reserve(count);

        for (auto root: roots)
        {
            eastl::stack<eastl::pair<Entity, uint32_t>> traversalStack;
            traversalStack.emplace(root, 0);
            while (!traversalStack.empty())
            {
                auto cur = traversalStack.top();
                traversalStack.pop();

                result.emplace_back(cur);

                eastl::vector<Entity> children = GetChildren(cur.first);
                uint32_t depth = cur.second + 1;
                for (auto it = children.rbegin(); it != children.rend(); it++)
                {
                    traversalStack.emplace(eastl::make_pair<Entity, uint32_t>(*it, depth));
                }
            }
        }

        if (result.size() != count)
        {
            LOG_ERROR("[HierarchyManager] An error has occurred in entity hierarchy.");
        }

        return result;
    }

    void HierarchyManager::SetParent(Entity entity, Entity parent, Entity prevSibling)
    {
        if (entity == NullEntity || parent == NullEntity)
        {
            LOG_ERROR("[HierarchyManager] SetParent: entity or parent is null");
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

        auto& context = *WorldExecuteContext::Current();
        Hierarchy entityHier;
        if (context.Has<Hierarchy>(entity))
        {
            entityHier = context.Get<Hierarchy>(entity);
        }

        // get next
        Entity next = NullEntity;
        if (prevSibling != NullEntity)
        {
            next = context.Get<Hierarchy>(prevSibling).nextSibling;
        }
        else
        {
            next = context.Get<Hierarchy>(parent).firstChild;
        }

        entityHier.parent = parent;
        entityHier.prevSibling = prevSibling;
        entityHier.nextSibling = next;
        context.AddOrReplace<Hierarchy>(entity, entityHier);

        // Remove root tag
        if (context.Has<HierarchyRootTag>(entity))
        {
            context.Remove<HierarchyRootTag>(entity);
        }
    }

    void HierarchyManager::SetParent(ContextStorage<Entity>& storage, Entity entity, Entity parent,
        Entity prevSibling)
    {
        if (entity == NullEntity || parent == NullEntity)
        {
            LOG_ERROR("[HierarchyManager] SetParent: entity or parent is null");
            return;
        }

        if (!storage.Has<Hierarchy>(parent))
        {
            storage.Add<Hierarchy>(parent);
        }
        if (prevSibling != NullEntity && !storage.Has<Hierarchy>(prevSibling))
        {
            storage.Add<Hierarchy>(prevSibling);
        }

        // No listener will finish the job on a storage, so both halves happen here: out of the old
        // position first, then into the new one.
        //
        // The entity keeps its own firstChild across the move: RemoveEntityInternal lifts the
        // children out and only AddEntityInternal puts them back, and it finds them through that
        // field.
        Hierarchy hierarchy;
        if (storage.Has<Hierarchy>(entity))
        {
            hierarchy = storage.Get<Hierarchy>(entity);
            RemoveEntityInternal(storage, hierarchy);
        }

        hierarchy.parent = parent;
        hierarchy.prevSibling = prevSibling;
        hierarchy.nextSibling = prevSibling != NullEntity
            ? storage.Get<Hierarchy>(prevSibling).nextSibling
            : storage.Get<Hierarchy>(parent).firstChild;

        // The world path pays for this check through the event it shares with hand-authored
        // Hierarchy components. Here it is the only thing standing between a caller's bad argument
        // -- a prevSibling that is not a child of parent -- and a corrupted list.
        if (!Valid(storage, hierarchy))
        {
            LOG_ERROR("[HierarchyManager] SetParent: hierarchy is invalid, the entity was not attached");
            return;
        }

        storage.AddOrReplace<Hierarchy>(entity, hierarchy);
        AddEntityInternal(storage, entity);
    }

    void HierarchyManager::PatchEntityHierarchy(Entity entity, eastl::function<void(Entity)> func)
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

    bool HierarchyManager::Valid(const ContextStorage<Entity>& context, const Hierarchy& hierarchy) const
    {
        if (hierarchy.parent != NullEntity && !context.Has<Hierarchy>(hierarchy.parent))
        {
            LOG_ERROR("[HierarchyManager] Valid: Entity has parent but the parent entity has no Hierarchy.");
            return false;
        }

        if (hierarchy.prevSibling != NullEntity && !context.Has<Hierarchy>(hierarchy.prevSibling))
        {
            LOG_ERROR("[HierarchyManager] Valid: Entity has prevSibling but the prevSibling entity has no Hierarchy.");
            return false;
        }

        if (hierarchy.nextSibling != NullEntity && !context.Has<Hierarchy>(hierarchy.nextSibling))
        {
            LOG_ERROR("[HierarchyManager] Valid: Entity has nextSibling but the nextSibling entity has no Hierarchy.");
            return false;
        }

        if (hierarchy.firstChild != NullEntity && !context.Has<Hierarchy>(hierarchy.firstChild))
        {
            LOG_ERROR("[HierarchyManager] Valid: Entity has firstChild but the firstChild entity has no Hierarchy.");
            return false;
        }

        if (hierarchy.nextSibling != NullEntity || hierarchy.prevSibling != NullEntity)
        { 
            if (hierarchy.parent == NullEntity)
            {
                LOG_ERROR("[HierarchyManager] Valid: Entity has sibling but it does not has a parent.");
                return false;
            }

            if (hierarchy.prevSibling != NullEntity)
            {
                Entity siblingParent = context.Get<Hierarchy>(hierarchy.prevSibling).parent;
                if (siblingParent != hierarchy.parent)
                {
                    LOG_ERROR("[HierarchyManager] Valid: Entity and its previous sibling has a different parent.");
                    return false;
                }
                Entity next = context.Get<Hierarchy>(hierarchy.prevSibling).nextSibling;
                if (next != hierarchy.nextSibling)
                {
                    LOG_ERROR("[HierarchyManager] Valid: The previous sibling has a next entity and it is different from the nextSibling in this Hierarchy.");
                    return false;
                }
            }

            if (hierarchy.nextSibling != NullEntity)
            {
                Entity siblingParent = context.Get<Hierarchy>(hierarchy.nextSibling).parent;
                if (siblingParent != hierarchy.parent)
                {
                    LOG_ERROR("[HierarchyManager] Valid: Entity and its next sibling has a different parent.");
                    return false;
                }
                Entity prev = context.Get<Hierarchy>(hierarchy.nextSibling).prevSibling;
                if (prev != hierarchy.prevSibling)
                {
                    LOG_ERROR("[HierarchyManager] Valid: The next sibling has a previous entity and it is different from the prevSibling in this Hierarchy.");
                    return false;
                }
            }

            if (hierarchy.prevSibling != NullEntity && hierarchy.nextSibling != NullEntity)
            {
                Entity next = context.Get<Hierarchy>(hierarchy.prevSibling).nextSibling;
                Entity prev = context.Get<Hierarchy>(hierarchy.nextSibling).prevSibling;
                if (next != prev)
                {
                    LOG_ERROR("[HierarchyManager] Valid: Hierarchy both set prevSibling and nextSibling but they are not adjacent now.");
                    return false;
                }
            }
        }
        else  // both prev and next are null
        {
            if (hierarchy.parent != NullEntity)
            {
                if (context.Get<Hierarchy>(hierarchy.parent).firstChild != NullEntity)
                {
                    LOG_ERROR("[HierarchyManager] Valid: The parent entity already has child, "
                        "but Hierarchy have not specified the insertion position for this entity."
                        "(Both prevSibling and nextSibling are null)");
                    return false;
                }
            }
        }

        return true;
    }

    void HierarchyManager::ForEachChild(const ContextStorage<Entity>& context, const Hierarchy& hierarchy,
        eastl::function<void(Entity entity)> func)
    {
        Entity cur = hierarchy.firstChild;
        while(cur != NullEntity && context.Has<Hierarchy>(cur))
        {
            func(cur);
            cur = context.Get<Hierarchy>(cur).nextSibling;
        }
    }

    void HierarchyManager::RemoveEntityInternal(ContextStorage<Entity>& context, const Hierarchy& hierarchy)
    {
        Entity parent = hierarchy.parent;
        Entity prevSibling = hierarchy.prevSibling;
        Entity nextSibling = hierarchy.nextSibling;
        Entity firstChild = hierarchy.firstChild;

        if (parent != NullEntity)
        {
            if (prevSibling == NullEntity)
            {
                auto& parentHier = context.Get<Hierarchy>(parent);
                parentHier.firstChild = nextSibling;
            }
        }

        // 子节点上升至父节点的子节点
        Entity cur = firstChild;
        Entity first = cur;
        Entity last = cur;
        bool isFirst = true;
        ForEachChild(context, hierarchy, [&](Entity child){
            auto& curHier = context.Get<Hierarchy>(child);
            if (isFirst && parent != NullEntity)
            {
                auto& parentHier =  context.Get<Hierarchy>(parent);
                if (parentHier.firstChild == NullEntity)
                {
                    parentHier.firstChild = child;
                }
                isFirst = false;
            }
            curHier.parent = parent;
            
            if (curHier.parent == NullEntity && !context.Has<HierarchyRootTag>(child))
            {
                context.Add<HierarchyRootTag>(child);
            }

            last = child;
        });

        if (prevSibling != NullEntity)
        {
            auto& prevSiblingHier = context.Get<Hierarchy>(prevSibling);
            if (first != NullEntity)
            {
                auto& firstHier = context.Get<Hierarchy>(first);
                firstHier.prevSibling = prevSibling;
                prevSiblingHier.nextSibling = first;
            }
            else
            {
                prevSiblingHier.nextSibling = nextSibling;
            }
        }

        if (nextSibling != NullEntity)
        {
            auto& nextSiblingHier = context.Get<Hierarchy>(nextSibling);
            if (last != NullEntity)
            {
                auto& lastHier = context.Get<Hierarchy>(last);
                lastHier.nextSibling = nextSibling;
                nextSiblingHier.prevSibling = last;
            }
            else
            {
                nextSiblingHier.prevSibling = prevSibling;
            }
        }
    }

    void HierarchyManager::AddEntityInternal(ContextStorage<Entity>& context, Entity entity)
    {
        const auto hier = context.Get<Hierarchy>(entity);

        Entity parent = hier.parent;
        Entity prevSibling = hier.prevSibling;
        Entity nextSibling = hier.nextSibling;
        Entity firstChild = hier.firstChild;

        if (parent != NullEntity)
        {
            // 插入至第一个子节点
            if (prevSibling == NullEntity)
            {
                auto& parentHier = context.Get<Hierarchy>(parent);
                parentHier.firstChild = entity;
            }

            if (context.Has<HierarchyRootTag>(entity))
            {
                context.Remove<HierarchyRootTag>(entity);
            }
        }

        if (prevSibling == NullEntity && nextSibling != NullEntity)
        {
            prevSibling = context.Get<Hierarchy>(nextSibling).prevSibling;
        }

        if (prevSibling != NullEntity && nextSibling == NullEntity)
        {
            nextSibling = context.Get<Hierarchy>(prevSibling).nextSibling;
        }

        if (prevSibling != NullEntity)
        {
            auto& prevSiblingHier = context.Get<Hierarchy>(prevSibling);
            prevSiblingHier.nextSibling = entity;
        }

        if (nextSibling != NullEntity)
        {
            auto& nextSiblingHier = context.Get<Hierarchy>(nextSibling);
            nextSiblingHier.prevSibling = entity;
        }

        bool isFirst = true;
        ForEachChild(context, hier, [&](Entity child){
            auto& curHier = context.Get<Hierarchy>(child);
            curHier.parent = entity;
            if (context.Has<HierarchyRootTag>(child))
            {
                context.Remove<HierarchyRootTag>(child);
            }

            if (isFirst)
            {
                Entity prev = curHier.prevSibling;
                if (prev != NullEntity)
                {
                    auto& hier = context.Get<Hierarchy>(prev);
                    hier.nextSibling = NullEntity;
                    Entity oldParent = hier.parent;
                }
                curHier.prevSibling = NullEntity;
                isFirst = false;
            }
        });
    }

    void HierarchyManager::OnComponentConstruct(Entity entity)
    {
        auto& context = *WorldExecuteContext::Current();
        const auto& hier = context.Get<Hierarchy>(entity);
        if (!Valid(context, hier))
        {
            LOG_ERROR("[HierarchyManager] OnComponentConstruct: Hierarchy is invalid, will remove the hierarchy");
            ComponentEventBus::Handler::BusDisconnect();
            context.Remove<Hierarchy>(entity);
            ComponentEventBus::Handler::BusConnect(GetTypeId<Hierarchy>());
            return;
        }
        
        AddEntityInternal(context, entity);

        if (hier.parent == NullEntity && !context.Has<HierarchyRootTag>(entity))
        {
            context.Add<HierarchyRootTag>(entity);
        }
    }

    void HierarchyManager::OnComponentsConstruct(eastl::span<const Entity> entities)
    {
        auto& context = *WorldExecuteContext::Current();

        for (Entity entity : entities)
        {
            const auto& hier = context.Get<Hierarchy>(entity);

            // References inside the batch already form a consistent tree; only edges that leave it
            // need linking in. An entity carrying MergedFrom is one of the arrivals.
            const bool parentInBatch = hier.parent != NullEntity && context.Has<MergedFrom<Entity>>(hier.parent);
            if (!parentInBatch)
            {
                if (!Valid(context, hier))
                {
                    LOG_ERROR("[HierarchyManager] OnComponentsConstruct: Hierarchy is invalid, will remove the hierarchy");
                    ComponentEventBus::Handler::BusDisconnect();
                    context.Remove<Hierarchy>(entity);
                    ComponentEventBus::Handler::BusConnect(GetTypeId<Hierarchy>());
                    continue;
                }

                AddEntityInternal(context, entity);
            }

            if (hier.parent == NullEntity && !context.Has<HierarchyRootTag>(entity))
            {
                context.Add<HierarchyRootTag>(entity);
            }
        }
    }

    void HierarchyManager::OnComponentWillUpdate(Entity entity)
    {
        auto& context = *WorldExecuteContext::Current();
        const auto& oldHier = context.Get<Hierarchy>(entity);

        RemoveEntityInternal(context, oldHier);
    }

    void HierarchyManager::OnComponentUpdated(Entity entity)
    {
        auto& context = *WorldExecuteContext::Current();
        const auto& hier = context.Get<Hierarchy>(entity);
        if (!Valid(context, hier))
        {
            LOG_ERROR("[HierarchyManager] OnComponentUpdated: Hierarchy is invalid, will remove the hierarchy");
            ComponentEventBus::Handler::BusDisconnect();
            context.Remove<Hierarchy>(entity);
            ComponentEventBus::Handler::BusConnect(GetTypeId<Hierarchy>());
            return;
        }

        AddEntityInternal(context, entity);
        if (hier.parent == NullEntity && !context.Has<HierarchyRootTag>(entity))
        {
            context.Add<HierarchyRootTag>(entity);
        }
    }

    void HierarchyManager::OnComponentDestory(Entity entity)
    {
        auto& context = *WorldExecuteContext::Current();
        const auto& hier = context.Get<Hierarchy>(entity);
        RemoveEntityInternal(context, hier);
        
        if (context.Has<HierarchyRootTag>(entity))
        {
            context.Remove<HierarchyRootTag>(entity);
        }
    }
}