#pragma once

#include <thread>

#include <Log/SpdLogSystem.h>

namespace Spark
{
    template <typename InterfaceType, typename Traits>
    class EBus;

    // 一个链表结构的节点，模拟调用栈
    template <typename InterfaceType, typename Traits>
    struct CallstackEntryBase
    {
        using BusType = EBus<InterfaceType, Traits>;

        CallstackEntryBase(const typename Traits::BusIdType* id)
            : m_busId(id)
        { }

        virtual ~CallstackEntryBase() = default;

        virtual void OnRemoveHandler(InterfaceType* handler)
        {
            if (m_prev)
            {
                m_prev->OnRemoveHandler(handler);
            }
        }

        virtual void OnPostRemoveHandler()
        {
            if (m_prev)
            {
                m_prev->OnPostRemoveHandler();
            }
        }

        const typename Traits::BusIdType* m_busId;
        CallstackEntryBase<InterfaceType, Traits>* m_prev = nullptr;
    };

    // Single threaded callstack entry
    template <typename InterfaceType, typename Traits>
    struct CallstackEntry
        : public CallstackEntryBase<InterfaceType, Traits>
    {
        using BusType = EBus<InterfaceType, Traits>;
        using BusContextPtr = typename BusType::Context*;

        CallstackEntry(BusContextPtr context, const typename Traits::BusIdType* busId)
            : CallstackEntryBase<InterfaceType, Traits>(busId)
            , m_threadId(std::this_thread::get_id())
        {
            assert(context && "Internal error: context deleted while execution still in progress.");
            m_context = context;

            this->m_prev = m_context->s_callstack->m_prev;

            if (!this->m_prev || static_cast<CallstackEntry*>(this->m_prev)->m_threadId == m_threadId)
            {
                m_context->s_callstack->m_prev = this;

                m_context->m_dispatches++;
            }
            else
            {
                LOG_INFO("[EBus] Bus has multiple threads in its callstack records. Configure MutexType on the bus, or don't send to it from multiple threads");
            }
        }

        CallstackEntry(CallstackEntry&&) = default;

        CallstackEntry(const CallstackEntry&) = delete;
        CallstackEntry& operator=(const CallstackEntry&) = delete;
        CallstackEntry& operator=(CallstackEntry&&) = delete;

        ~CallstackEntry() override
        {
            m_context->m_dispatches--;

            m_context->s_callstack->m_prev = this->m_prev;
        }

        BusContextPtr m_context = nullptr;
        std::thread::id m_threadId;
    };

    // One of these will be allocated per thread. It acts as the bottom of any callstack during dispatch within
    // that thread. It has to be stored in the context so that it is shared across DLLs. We accelerate this by
    // caching the root into a thread_local pointer (Context::s_callstack) on first access. Since global bus contexts
    // never die, the TLS pointer does not need to be lifetime managed.
    template <typename InterfaceType, typename Traits>
    struct CallstackEntryRoot
        : public CallstackEntryBase<InterfaceType, Traits>
    {
        using BusType = EBus<InterfaceType, Traits>;

        CallstackEntryRoot()
            : CallstackEntryBase<InterfaceType, Traits>(nullptr)
        {}

        void OnRemoveHandler(InterfaceType*) override { assert(false && "Callstack root should never attempt to handle the removal of a bus handler"); }
        void OnPostRemoveHandler() override { assert(false && "Callstack root should never attempt to handle the removal of a bus handler"); }
    };

    template <class C, bool UseTLS /*= false*/>
    struct EBusCallstackStorage
    {
        C* m_entry = nullptr;

        EBusCallstackStorage() = default;
        ~EBusCallstackStorage() = default;
        EBusCallstackStorage(const EBusCallstackStorage&) = delete;
        EBusCallstackStorage(EBusCallstackStorage&&) = delete;

        C* operator->() const
        {
            return m_entry;
        }

        C& operator*() const
        {
            return *m_entry;
        }

        C* operator=(C* entry)
        {
            m_entry = entry;
            return m_entry;
        }

        operator C*() const
        {
            return m_entry;
        }
    };

    template <class C>
    struct EBusCallstackStorage<C, true>
    {
        EBusCallstackStorage() = default;
        ~EBusCallstackStorage() = default;
        EBusCallstackStorage(const EBusCallstackStorage&) = delete;
        EBusCallstackStorage(EBusCallstackStorage&&) = delete;

        C* operator->() const
        {
            return GetEntry();
        }

        C& operator*() const
        {
            return *GetEntry();
        }

        C* operator=(C* entry)
        {
            GetEntry() = entry;
            return GetEntry();
        }

        operator C*() const
        {
            return GetEntry();
        }

    private:
        C*& GetEntry() const;
    };

    // This functino needs to be defined outside of the class definition such that it can get explicitly instantiated correctly. This is
    // important when using an EBus across module boundaries, as otherwise a callstack cannot traverse the module boundary.
    template <class C>
    C*& EBusCallstackStorage<C, true>::GetEntry() const
    {
        static thread_local C* s_entry = nullptr;
        return s_entry;
    }
}
