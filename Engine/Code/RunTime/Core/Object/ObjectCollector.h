/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/functional.h>
#include <EASTL/vector.h>

#include "Base.h"
#include "Object.h"

#include <Log/SpdLogSystem.h>

namespace Spark
{
    struct NullMutex
    {
        inline void lock() {}
        inline bool try_lock() { return true; }
        inline void unlock() {}
    };

    struct ObjectCollectorTraits
    {
        using ObjectType = Object;

        using MutexType = NullMutex;
    };

    using ObjectCollectorNotifyFunction = eastl::function<void()>;

    template <typename Traits = ObjectCollectorTraits>
    class ObjectCollector final
    {
    public:
        using ObjectType = typename Traits::ObjectType;
        using ObjectPtrType = Ptr<ObjectType>;

        ObjectCollector() = default;
        ~ObjectCollector();

        using CollectFunction = eastl::function<void(ObjectType&)>;

        struct Descriptor
        {
            /// Number of collect calls before an object is collected.
            size_t m_collectLatency = 0;

            /// The collector function called when an object is collected.
            CollectFunction m_collectFunction = nullptr;
        };

        void Init(const Descriptor& descriptor);
        void Shutdown();

        //! Queues a single pointer for collection.
        void QueueForCollect(ObjectPtrType object);

        //! Queues an array of objects for collection.
        void QueueForCollect(ObjectType* objects, size_t objectCount);

        void Collect(bool forceFlush = false);

        size_t GetObjectCount() const;

        void Notify(ObjectCollectorNotifyFunction notifyFunction);

    private:
        void QueueForCollectInternal(ObjectPtrType object);

        // Garbage = {m_pendingObjects, m_currentIteration, m_pendingNotifies}
        struct Garbage
        {
            eastl::vector<ObjectPtrType> m_objects;
            uint64_t m_collectIteration;
            eastl::vector<ObjectCollectorNotifyFunction> m_notifies;
        };

        inline bool IsGarbageReady(size_t collectIteration)
        {
            return m_currentIteration - collectIteration >= m_descriptor.m_collectLatency;
        }

        Descriptor m_descriptor;

        size_t m_currentIteration = 0;

        mutable typename Traits::MutexType m_mutex;
        eastl::vector<ObjectPtrType> m_pendingObjects;
        eastl::vector<Garbage> m_pendingGarbage;
        eastl::vector<ObjectCollectorNotifyFunction> m_pendingNotifies;
    };

    template <typename Traits>
    ObjectCollector<Traits>::~ObjectCollector()
    {
        if (!m_pendingGarbage.empty())
        {
            LOG_ERROR("There is garbage that wasn't collected");
        }
    }

    template <typename Traits>
    void ObjectCollector<Traits>::Init(const Descriptor& descriptor)
    {
        m_descriptor = descriptor;
    }

    template <typename Traits>
    void ObjectCollector<Traits>::Shutdown()
    {
        Collect(true);
    }

    template <typename Traits>
    void ObjectCollector<Traits>::QueueForCollect(ObjectPtrType object)
    {
        m_mutex.lock();
        QueueForCollectInternal(eastl::move(object));
        m_mutex.unlock();
    }

    template <typename Traits>
    void ObjectCollector<Traits>::QueueForCollect(ObjectType* objects, size_t objectCount)
    {
        m_mutex.lock();
        for (size_t i = 0; i < objectCount; ++i)
        {
            QueueForCollectInternal(&objects[i]);
        }
        m_mutex.unlock();
    }

    template <typename Traits>
    void ObjectCollector<Traits>::QueueForCollectInternal(ObjectPtrType object)
    {
        if (!object)
        {
            LOG_ERROR("Attempted to queue a null object for collection");
            return;
        }

        m_pendingObjects.emplace_back(eastl::move(object));
    }

    template <typename Traits>
    void ObjectCollector<Traits>::Collect(bool forceFlush)
    {
        m_mutex.lock();
        if (m_pendingObjects.size())
        {
            m_pendingGarbage.emplace_back({eastl::move(m_pendingObjects), m_currentIteration});
        }

        if (!m_pendingNotifies.empty())
        {
            if (!m_pendingGarbage.empty())
            {
                // find the newest garbage entry and add any pending notifies
                Garbage& latestGarbage = m_pendingGarbage.front();
                size_t latestGarbageAge = m_currentIteration - latestGarbage.m_collectIteration;

                // check the rest of the entries to see if they are newer
                for (size_t i = 1; i < m_pendingGarbage.size(); ++i)
                {
                    size_t age = m_currentIteration - m_pendingGarbage[i].m_collectIteration;
                    if (age < latestGarbageAge)
                    {
                        latestGarbage = m_pendingGarbage[i];
                        latestGarbageAge = age;
                    }
                }

                latestGarbage.m_notifies.insert(latestGarbage.m_notifies.end(), m_pendingNotifies.begin(), m_pendingNotifies.end());
            }
            else
            {
                // garbage queue is empty, notify now
                for (auto& notifyFunction : m_pendingNotifies)
                {
                    notifyFunction();
                }
            }

            m_pendingNotifies.clear();
        }
        m_mutex.unlock();

        size_t objectCount = 0;
        size_t i = 0;
        while (i < m_pendingGarbage.size())
        {
            Garbage& garbage = m_pendingGarbage[i];
            if (IsGarbageReady(garbage.m_collectIteration) || forceFlush)
            {
                if (m_descriptor.m_collectFunction)
                {
                    for (ObjectPtrType& object : garbage.m_objects)
                    {
                        m_descriptor.m_collectFunction(*object);
                    }
                }
                objectCount += garbage.m_objects.size();

                for (auto& notifyFunction : garbage.m_notifies)
                {
                    notifyFunction();
                }

                garbage = eastl::move(m_pendingGarbage.back());
                m_pendingGarbage.pop_back();
            }
            else
            {
                ++i;
            }
        }
        m_currentIteration++;
    }

    template <typename Traits>
    size_t ObjectCollector<Traits>::GetObjectCount() const
    {
        size_t objectCount = 0;
        m_mutex.lock();
        objectCount += m_pendingObjects.size();
        m_mutex.unlock();

        for (const Garbage& garbage : m_pendingGarbage)
        {
            objectCount += garbage.m_objects.size();
        }

        return objectCount;
    }

    template <typename Traits>
    void ObjectCollector<Traits>::Notify(ObjectCollectorNotifyFunction notifyFunction)
    {
        m_mutex.lock();
        m_pendingNotifies.push_back(notifyFunction);
        m_mutex.unlock();
    }
}