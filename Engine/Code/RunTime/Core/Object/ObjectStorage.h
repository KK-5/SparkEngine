#pragma once

#include <EASTL/queue.h>

namespace Spark
{
    template<typename ObjectType, typename StoragePolicy>
    class ObjectFreeList
    {
    public:
        using ValueType = ObjectType*;
        using StorageType = StoragePolicy<ValueType>;

        void Add(ValueType object)
        {
            m_storage.Add(object);
        }

        void Remove(ValueType object)
        {
            m_storage.Remove(object);
        }

        void First()
        {
            m_storage.First();
        }

        bool IsEmpty() const
        {
            return m_storage.IsEmpty();
        }

    private:
        StorageType m_storage;
    };

    template<typename T>
    class QueueStoragePolicy
    {
    public:
        void Add(T object)
        {
            m_queue.push(object);
        }

        void Remove(T object)
        {
            m_queue.pop();
        }

        void First()
        {
            // Implementation for accessing the first object in the queue
        }

        bool IsEmpty() const
        {
            return m_queue.empty();
        }

    private:
        eastl::queue<T> m_queue;
    };
}