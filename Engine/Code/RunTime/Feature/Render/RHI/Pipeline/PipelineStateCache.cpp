/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "PipelineStateCache.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
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
    }
}