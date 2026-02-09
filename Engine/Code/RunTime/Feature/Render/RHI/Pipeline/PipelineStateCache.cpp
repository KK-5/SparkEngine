/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

 /*
 * Modified by SparkEngine in 2025
 *  -- Only the global PipelineState cache is being used, thread-local and global pending caches are not in use.They will be implemented in the future.
 */

#include "PipelineStateCache.h"

#include <Log/SpdLogSystem.h>

#include <RHI/Factory.h>

namespace Spark::RHI
{
    bool PipelineStateEntry::operator == (const PipelineStateEntry& rhs) const
    {
        if(eastl::get_if<PipelineStateDescriptorForDispatch>(&rhs.m_pipelineStateDescriptorVariant) &&
            eastl::get_if<PipelineStateDescriptorForDispatch>(&m_pipelineStateDescriptorVariant))
        {
            const PipelineStateDescriptorForDispatch& lhsDesc = eastl::get<PipelineStateDescriptorForDispatch>(m_pipelineStateDescriptorVariant);
            const PipelineStateDescriptorForDispatch& rhsDesc = eastl::get<PipelineStateDescriptorForDispatch>(rhs.m_pipelineStateDescriptorVariant);

            return lhsDesc == rhsDesc;
        }
        else if(eastl::get_if<PipelineStateDescriptorForDraw>(&rhs.m_pipelineStateDescriptorVariant) &&
            eastl::get_if<PipelineStateDescriptorForDraw>(&m_pipelineStateDescriptorVariant))
        {
            const PipelineStateDescriptorForDraw& lhsDesc = eastl::get<PipelineStateDescriptorForDraw>(m_pipelineStateDescriptorVariant);
            const PipelineStateDescriptorForDraw& rhsDesc = eastl::get<PipelineStateDescriptorForDraw>(rhs.m_pipelineStateDescriptorVariant);

            return lhsDesc == rhsDesc;
        }
        else if(eastl::get_if<PipelineStateDescriptorForRayTracing>(&rhs.m_pipelineStateDescriptorVariant) &&
            eastl::get_if<PipelineStateDescriptorForRayTracing>(&m_pipelineStateDescriptorVariant))
        {
            const PipelineStateDescriptorForRayTracing& lhsDesc = eastl::get<PipelineStateDescriptorForRayTracing>(m_pipelineStateDescriptorVariant);
            const PipelineStateDescriptorForRayTracing& rhsDesc = eastl::get<PipelineStateDescriptorForRayTracing>(rhs.m_pipelineStateDescriptorVariant);

            return lhsDesc == rhsDesc;
        }

        return false;
    }

    void PipelineStateCache::ValidateCacheIntegrity() const
    {
        for (size_t i = 0; i < m_globalLibrarySet.size(); ++i)
        {
            const GlobalLibraryEntry& globalLibraryEntry = m_globalLibrarySet[i];
            const PipelineStateSet& readOnlyCache = globalLibraryEntry.m_readOnlyCache;
            ASSERT(globalLibraryEntry.m_pendingCompileCount == 0, "Compiles are pending for pipeline library");
            ASSERT(globalLibraryEntry.m_pendingCache.empty(), "Pending cache is not empty.");

            if (!m_globalLibraryActiveBits[i])
            {
                ASSERT(readOnlyCache.empty(), "Inactive library has pipeline states in its global entry.");
            }
        }

        /*
        eastl::for_each(m_threadLibrarySet.begin(), m_threadLibrarySet.end(), [this](const ThreadLibrarySet& threadLibrarySet)
        {
            const size_t libraryCount = m_globalLibrarySet.size();

            for (size_t i = 0; i < libraryCount; ++i)
            {
                const ThreadLibraryEntry& threadLibraryEntry = threadLibrarySet[i];

                if (!m_globalLibraryActiveBits[i])
                {
                    ASSERT(!threadLibraryEntry.m_library, "Inactive library has a valid RHI::PipelineLibrary instance.");
                }

                ASSERT(threadLibraryEntry.m_threadLocalCache.empty(), "Thread library should not have any items in its local cache.");
            }
        });
        */
    }

    void PipelineStateCache::Reset()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        for (size_t i = 0; i < m_globalLibrarySet.size(); ++i)
        {
            if (m_globalLibraryActiveBits[i])
            {
                ResetLibraryImpl(i);
            }
        }
    }

    PipelineLibraryIndex PipelineStateCache::CreateLibrary()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        PipelineLibraryIndex index;
        if (!m_libraryFreeList.empty())
        {
            index = m_libraryFreeList.back();
            m_libraryFreeList.pop_back();
        }
        else
        {
            if (m_globalLibrarySet.size() == LibraryCountMax)
            {
                LOG_ERROR(
                    "[PipelineStateCache]",
                    "Exceeded maximum number of allowed pipeline libraries in "
                    "cache. You must update LibraryCountMax to add more.");
                return InvalidPipelineLibraryIndex;
            }

            m_globalLibrarySet.emplace_back();
            index = m_globalLibrarySet.size();
        }

        ASSERT(m_globalLibraryActiveBits[index] == false, "Attempted to allocate active library entry!");
        m_globalLibraryActiveBits[index] = true;

        GlobalLibraryEntry& libraryEntry = m_globalLibrarySet[index];
        ASSERT(libraryEntry.m_readOnlyCache.empty() && libraryEntry.m_pendingCache.empty(), "Library entry has entries in its caches!");

        return index;
    }

    void PipelineStateCache::ReleaseLibrary(PipelineLibraryIndex index)
    {
        if (index != InvalidPipelineLibraryIndex)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            ASSERT(m_globalLibraryActiveBits[index], "Releasing a library that is no longer valid.");

            ResetLibraryImpl(index);

            GlobalLibraryEntry& libraryEntry = m_globalLibrarySet[index];
            libraryEntry.m_readOnlyCache.clear();

            m_globalLibraryActiveBits[index] = false;
            m_libraryFreeList.push_back(index);
        }
    }

    void PipelineStateCache::ResetLibrary(PipelineLibraryIndex index)
    {
        if (index != InvalidPipelineLibraryIndex)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            ResetLibraryImpl(index);
        }
    }

    void PipelineStateCache::ResetLibraryImpl(PipelineLibraryIndex index)
    {
        GlobalLibraryEntry& libraryEntry = m_globalLibrarySet[index];

        ASSERT(libraryEntry.m_pendingCompileCount == 0, "Reseting library while compiles are still pending!");
        libraryEntry.m_readOnlyCache.clear();
        libraryEntry.m_pendingCacheMutex.lock();
        libraryEntry.m_pendingCache.clear();
        libraryEntry.m_pendingCacheMutex.unlock();
    }

    const PipelineState* PipelineStateCache::AcquirePipelineState(PipelineLibraryIndex library, const PipelineStateDescriptor& descriptor, const ObjectName& name)
    {
        if (library == InvalidPipelineLibraryIndex)
        {
            return nullptr;
        }

        // std::shared_lock<std::shared_mutex> lock(m_mutex);

        // 这里暂时替换成unique_lock
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        GlobalLibraryEntry& globalLibraryEntry = m_globalLibrarySet[library];
        PipelineStateHash pipelineStateHash = descriptor.GetHash();

        // Search the read-only cache first.
        if (const PipelineState* pipelineState = FindPipelineState(globalLibraryEntry.m_readOnlyCache, descriptor))
        {
            return pipelineState;
        }

        // Search the thread-local cache next.
        // [TODO]

        // Lazy-init the library on first access.
        if (!globalLibraryEntry.m_library)
        {
            Ptr<PipelineLibrary> pipelineLibrary = Service<RHI::Factory>::Get()->CreatePipelineLibrary();
            RHI::ResultCode resultCode = pipelineLibrary->Init(GetDevice(), globalLibraryEntry.m_pipelineLibraryDescriptor);
            if (resultCode != RHI::ResultCode::Success)
            {
                LOG_WARN("[PipelineStateCache] Failed to initialize pipeline library. PipelineLibrary usage is disabled.");
            }

            globalLibraryEntry.m_library = eastl::move(pipelineLibrary);
        }

        ConstPtr<PipelineState> pipelineState =
            CompilePipelineState(globalLibraryEntry, descriptor, pipelineStateHash, name);

        [[maybe_unused]] bool success =
            InsertPipelineState(globalLibraryEntry.m_readOnlyCache, PipelineStateEntry(pipelineStateHash, pipelineState, descriptor));
        ASSERT(success, "PipelineStateEntry already exists in the thread cache.");

        return pipelineState.get();
    }

    const PipelineState* PipelineStateCache::FindPipelineState(const PipelineStateSet& pipelineStateSet, const PipelineStateDescriptor& descriptor)
    {
        auto pipelineStateIt = pipelineStateSet.find(PipelineStateEntry(descriptor.GetHash(), nullptr, descriptor));
        if (pipelineStateIt != pipelineStateSet.end())
        {
            return pipelineStateIt->m_pipelineState.get();
        }
        return nullptr;
    }

    bool PipelineStateCache::InsertPipelineState(PipelineStateSet& pipelineStateSet, PipelineStateEntry pipelineStateEntry)
    {
        auto ret = pipelineStateSet.insert(pipelineStateEntry);
        return ret.second;
    }

    ConstPtr<PipelineState> PipelineStateCache::CompilePipelineState(
        GlobalLibraryEntry& globalLibraryEntry,
        const PipelineStateDescriptor& pipelineStateDescriptor,
        PipelineStateHash pipelineStateHash,
        const ObjectName& name)
    {
        Ptr<PipelineState> pipelineState = Service<RHI::Factory>::Get()->CreatePipelineState();;

        // We no longer have the lock, but we own compilation of the pipeline state. Use the
        // thread-local library to perform compilation without blocking other threads.
        ResultCode resultCode = pipelineState->Init(GetDevice(), pipelineStateDescriptor, globalLibraryEntry.m_library.get());

        if (resultCode == ResultCode::Success)
        {
            return pipelineState;
        }

        return nullptr;
    }
}