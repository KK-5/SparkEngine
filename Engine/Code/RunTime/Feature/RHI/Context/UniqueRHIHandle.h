#pragma once

#include <CoreComponents/Tags.h>

#include "RHIContext.h"

namespace Spark::RHI
{
    //! An RHIContext entity its holder owns: marked dead when this goes, so the holder being
    //! removed, destroyed or cleared away is all it takes to let the entity go.
    //!
    //! Marked, not destroyed: whatever watches DeadTag on the entity sees it leave first.
    //!
    //! The destructor writes the context and can run while one is being torn down, so it
    //! does nothing without a current context or once the entity is no longer in it.
    class UniqueRHIHandle
    {
    public:
        UniqueRHIHandle() = default;

        explicit UniqueRHIHandle(RHIHandle handle)
            : m_handle(handle)
        {
        }

        UniqueRHIHandle(const UniqueRHIHandle&) = delete;
        UniqueRHIHandle& operator=(const UniqueRHIHandle&) = delete;

        UniqueRHIHandle(UniqueRHIHandle&& other) noexcept
            : m_handle(other.m_handle)
        {
            other.m_handle = NullHandle;
        }

        UniqueRHIHandle& operator=(UniqueRHIHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_handle       = other.m_handle;
                other.m_handle = NullHandle;
            }
            return *this;
        }

        ~UniqueRHIHandle()
        {
            Reset();
        }

        bool IsValid() const { return m_handle != NullHandle; }

        RHIHandle Get() const { return m_handle; }

        void Reset()
        {
            if (m_handle == NullHandle)
            {
                return;
            }

            RHIContext* context = RHIExecuteContext::Current();
            if (context && context->Valid(m_handle) && !context->Has<DeadTag>(m_handle))
            {
                context->Add<DeadTag>(m_handle);
            }
            m_handle = NullHandle;
        }

    private:
        RHIHandle m_handle = NullHandle;
    };
}
