#pragma once

#include <EASTL/unordered_map.h>
#include <mutex>

namespace Spark::RHI
{
    class Resource;
    class ResourceView;

    template <typename DescriptorType, typename Hasher>
    class ResourceViewCache
    {
    public:
        virtual ~ResourceViewCache() = default;

        bool IsInResourceCache(const DescriptorType& viewDescriptor)
        {
            auto it = m_resourceViewCache.find(viewDescriptor);
            return it != m_resourceViewCache.end();
        }

        void EraseResourceView(typename ResourceView* resourceView) const
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            for (auto it = m_resourceViewCache.begin(); it != m_resourceViewCache.end(); ++it)
            {
                if (it->second == resourceView)
                {
                    m_resourceViewCache.erase(it);
                    return;
                }
            }
        }

        ResourceView* GetResourceView(const DescriptorType& viewDescriptor) const
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_resourceViewCache.find(viewDescriptor);
            if (it != m_resourceViewCache.end())
            {
                return it->second;
            }
            return nullptr;
        }

    private:
        mutable eastl::unordered_map<DescriptorType, ResourceView*, Hasher> m_resourceViewCache;
        mutable std::mutex m_cacheMutex;
    };
    
}