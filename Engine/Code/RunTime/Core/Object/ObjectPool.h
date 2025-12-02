/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <EASTL/queue.h>
#include <EASTL/unordered_set.h>
#include <mutex>

#include "Base.h"
#include "ObjectCollector.h"
#include "IObjectFactory.h"

namespace Spark
{
    class ObjectPoolTraits: public ObjectCollectorTraits /// Inherits from object collector traits since they share types.
    {
    protected:
        ~ObjectPoolTraits() = default;

    public:
        /// The object factory class used to manage creation and deletion of objects from the pool.
        /// Override with your own type.
        using ObjectFactoryType = IObjectFactory<ObjectType>;
    };

    template <typename Traits>
    class ObjectPool
    {
    public:

        //! The type of object created and managed by this pool. Must inherit from Object.
        using ObjectType = typename Traits::ObjectType;

        //! The type of the object factory. An instance of this object
        using ObjectFactoryType = typename Traits::ObjectFactoryType;

        //! The type of the descriptor used to initialize the object factory.
        using ObjectFactoryDescriptor = typename ObjectFactoryType::Descriptor;

        //! The type of object collector used by the object pool.
        using ObjectCollectorType = ObjectCollector<Traits>;

        using MutexType = typename Traits::MutexType;

        ObjectPool() = default;
        ObjectPool(ObjectPool& rhs) = delete;

        struct Descriptor : public ObjectFactoryDescriptor
        {
            /// The number of GC iterations before objects in the pool will be recycled. Most useful when
            /// matched to the GPU / CPU fence latency.
            uint32_t m_collectLatency = 0;
        };

        //! Initializes the pool to an empty state.
        void Init(const Descriptor& descriptor)
        {
            m_factory.Init(descriptor);

            typename ObjectCollectorType::Descriptor collectorDesc;
            collectorDesc.m_collectLatency = descriptor.m_collectLatency;
            collectorDesc.m_collectFunction = [this](ObjectType& object)
            {
                if (m_isInitialized && m_factory.RecycleObject(object))
                {
                    m_freeList.push(&object);
                }
                else
                {
                    m_factory.ShutdownObject(object, !m_isInitialized);
                    m_objects.erase(&object);
                }
            };

            m_collector.Init(collectorDesc);
            m_isInitialized = true;
        }

        //! Shutdown the pool. The user must re-initialize to use it again.
        void Shutdown()
        {
            m_isInitialized = false;
            m_collector.Shutdown();
            while (!m_freeList.empty())
            {
                m_freeList.pop();
            }
            for (auto& objectPtr : m_objects)
            {
                m_factory.ShutdownObject(*objectPtr, true);
            }
            m_objects.clear();
            m_factory.Shutdown();
        }

        //! Allocates an instance of an object from the pool. If no free object exists, it will
        //! create a new instance from the factory. If a free object exists, it will reuse that one.
        template <typename... Args>
        ObjectType* Allocate(Args&&... args)
        {
            ObjectType* objectForReset = nullptr;

            {
                std::lock_guard<MutexType> lock(m_mutex);
                if (!m_freeList.empty())
                {
                    objectForReset = m_freeList.front();
                    m_freeList.pop();
                }
                else
                {
                    Ptr<ObjectType> objectPtr = m_factory.CreateObject(eastl::forward<Args>(args)...);
                    if (objectPtr)
                    {
                        m_objects.emplace(objectPtr);
                    }
                    return objectPtr.get();
                }
            }

            if (objectForReset)
            {
                m_factory.ResetObject(*objectForReset, eastl::forward<Args>(args)...);
            }

            return objectForReset;
        }

        //! Frees an object back to the pool. Depending on the object collection latency, it may take several
        //! cycles before the object is reused again.
        void DeAllocate(ObjectType* object)
        {
            m_collector.QueueForCollect(object);
        }

        void DeAllocate(ObjectType* objects, size_t objectCount)
        {
            m_collector.QueueForCollect(objects, objectCount);
        }

        //! Performs an object collection cycle. Objects which are collected can be reused by Allocate.
        void Collect()
        {
            m_factory.BeginCollect();
            m_collector.Collect();
            m_factory.EndCollect();
        }

        //! Performs an object collection cycle that ignores the collect latency, processing all objects.
        void CollectForce()
        {
            m_factory.BeginCollect();
            m_collector.Collect(true);
            m_factory.EndCollect();
        }

        //! Returns the total number of objects in the pool.
        size_t GetObjectCount() const
        {
            return m_objects.size();
        }

        const ObjectFactoryType& GetFactory() const
        {
            return m_factory;
        }

    private:
        ObjectFactoryType m_factory;
        ObjectCollector<Traits> m_collector;
        eastl::unordered_set<Ptr<ObjectType>> m_objects;
        eastl::queue<ObjectType*> m_freeList;
        MutexType m_mutex;
        bool m_isInitialized = false;
    };
}