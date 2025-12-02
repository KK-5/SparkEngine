#include "Object.h"

namespace Spark
{
    void Object::SetName(const ObjectName& name)
    {
        m_name = name;
    }

    const ObjectName& Object::GetName() const
    {
        return m_name;
    }

    void Object::AddRef()
    {
        m_useCount.fetch_add(1, eastl::memory_order_relaxed);
    }

    void Object::Release()
    {
        if (m_useCount.fetch_sub(1) == 1)
        {
            Object* object = const_cast<Object*>(this);
            object->Shutdown();
            delete object;
        }
    }
}