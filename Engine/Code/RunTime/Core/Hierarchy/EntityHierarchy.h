#pragma once

#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/optional.h>
#include <EASTL/algorithm.h>

#include <ECS/Entity.h>

namespace Spark
{
    template<typename IDType = Entity>
    class EntityHierarchy
    {
        using EntityNode = IDType;
    public:
        EntityHierarchy() = default;
        virtual ~EntityHierarchy() = default;
        
        bool AddNode(EntityNode entity, eastl::optional<EntityNode> parent = eastl::nullopt)
        {
            if (Contain(entity))
            {
                return false;
            }
            
            if (parent.has_value() && !Contain(parent.value()))
            {
                return false;
            }

            m_entities.insert(entity);
            m_parentMap[entity] = parent;
            if (parent.has_value())
            {
                m_childrenMap[parent.value()].emplace_back(entity);
            }
            else
            {
                m_roots.emplace_back(entity); 
            }

            return true;
        }

        bool RemoveNode(EntityNode entity)
        {
            if (!Contain(entity))
            {
                return false;
            }

            eastl::vector<EntityNode> children = GetChildren(entity);
            for (auto child: children)
            {
                RemoveNode(child);
            }

            eastl::optional<EntityNode> parent = GetParent(entity);
            if (parent.has_value())
            {
                eastl::vector<EntityNode>& siblings = m_childrenMap[parent.value()];
                siblings.erase(eastl::remove(siblings.begin(), siblings.end(), entity), siblings.end());
            }
            else
            {
                m_roots.erase(eastl::remove(m_roots.begin(), m_roots.end(), entity), m_roots.end());
            }

            m_entities.erase(entity);
            m_childrenMap.erase(entity);
            m_parentMap.erase(entity);

            return true;
        }

        bool RemoveSingleNode(EntityNode entity)
        {
            if (!Contain(entity))
            {
                return false;
            }

            eastl::optional<EntityNode> parent = GetParent(entity);
            eastl::vector<EntityNode> children = GetChildren(entity);
            eastl::for_each(children.begin(), children.end(), [&](EntityNode entity)
            {
                m_parentMap[entity] = parent;
            });

            if (parent.has_value())
            {
                eastl::vector<EntityNode>& siblings = m_childrenMap[parent.value()];
                auto it = eastl::find(siblings.begin(), siblings.end(), entity);
                siblings.insert(it, children.begin(), children.end());
                siblings.erase(eastl::remove(siblings.begin(), siblings.end(), entity), siblings.end());
            }
            else
            {
                m_roots.erase(eastl::remove(m_roots.begin(), m_roots.end(), entity), m_roots.end());
            }

            m_entities.erase(entity);
            m_childrenMap.erase(entity);
            m_parentMap.erase(entity);

            return true;
        }

        bool MoveNode(EntityNode entity, eastl::optional<EntityNode> newParent)
        {
            if (!Contain(entity))
            {
                return false;
            }

            if (newParent.has_value() && !Contain(newParent.value()))
            {
                return false;
            }


            if (newParent.has_value() && IsAncestor(newParent.value(), entity))
            {
                return false;
            }
            
            // 从原父节点中删除
            eastl::optional<EntityNode> oldParent = GetParent(entity);
            if (oldParent.has_value())
            {
                eastl::vector<EntityNode>& siblings = m_childrenMap[oldParent.value()];
                siblings.erase(eastl::remove(siblings.begin(), siblings.end(), entity), siblings.end());
            }
            else
            {
                m_roots.erase(eastl::remove(m_roots.begin(), m_roots.end(), entity), m_roots.end());
            }
            
            // 更新到新父节点
            m_parentMap[entity] = newParent;
            if (newParent.has_value())
            {
                m_childrenMap[newParent.value()].emplace_back(entity);
            }
            else
            {
                m_roots.emplace_back(entity); 
            }
            return true;
        }

        bool HasChildren(EntityNode entity) const
        {
            return m_childrenMap.find(entity) != m_childrenMap.end();
        }

        eastl::vector<EntityNode> GetChildren(EntityNode entity) const 
        {
            eastl::vector<EntityNode> result;
            auto it = m_childrenMap.find(entity);
            if (it != m_childrenMap.end())
            {
                result = it->second;
            }
            return result;
        }

        eastl::optional<EntityNode> GetParent(EntityNode entity) const 
        {
            auto it = m_parentMap.find(entity);
            if (it != m_parentMap.end())
            {
               return it->second;
            }
            return eastl::nullopt;
        }

        eastl::vector<EntityNode> GetRoots() const
        {
            return m_roots;
        }

        eastl::unordered_set<EntityNode> GetNodes() const
        {
            return m_entities;
        }

        bool Contain(EntityNode entity) const 
        {
            return m_entities.find(entity) != m_entities.end();
        }

        size_t Size() const
        {
            return m_entities.size();
        }

        bool Empty() const
        {
            return m_entities.empty();
        }

        bool IsAncestor(EntityNode entity, EntityNode ancestor) const
        {
            if (!Contain(entity) || !Contain(ancestor))
            {
                return false;
            }

            if (entity == ancestor)
            {
                return true;
            }

            auto it = m_parentMap.find(entity);
            while(it != m_parentMap.end() && it->second.has_value())
            {
                if (it->second.value() == ancestor)
                {
                    return true;
                }
                it = m_parentMap.find(it->second.value());
            }
            return false;
        }

        eastl::vector<EntityNode> FindPath(EntityNode entity) const
        {
            eastl::vector<EntityNode> path;
            if (!Contain(entity))
            {
                return path;
            }

            auto it = m_parentMap.find(entity);
            while(it != m_parentMap.end() && it->second.has_value())
            {
                EntityNode parent = it->second.value();
                path.emplace_back(parent);
                it = m_parentMap.find(parent);
            }

            eastl::reverse(path.begin(), path.end());
            return path;
        }

        /*
        void DFSTraversal(EntityNode startNode, eastl::function<void(const EntityNode&)> visitor) const
        {
            if (!Contain(startNode))
            {
                return;
            }

            visitor(startNode);

            if (m_childrenMap.find(startNode) == m_childrenMap.end())
            {
                return;
            }

            eastl::vector<EntityNode> children = GetChildren(startNode);
            for(auto child: children)
            {
                DFSTraversal(child, visitor);
            }
        }
        */

        void Clear()
        {
            m_parentMap.clear();
            m_childrenMap.clear();
            m_roots.clear();
            m_entities.clear();
        }
    
    private:
        eastl::unordered_map<EntityNode, eastl::optional<EntityNode>> m_parentMap;
        eastl::unordered_map<EntityNode, eastl::vector<EntityNode>> m_childrenMap;
        eastl::vector<EntityNode> m_roots;
        eastl::unordered_set<EntityNode> m_entities;
    };
}