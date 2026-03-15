/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <mutex>
#include <RHI/Resource/ResourcePool.h>

#include "ShaderResourcePoolDescriptor.h"
#include "ShaderResourceLayout.h"
#include "ShaderResource.h"

namespace Spark::RHI
{
    class ShaderResourcePool : public ResourcePool
    {
    public:
        virtual ~ShaderResourcePool() = default;

        //! Initializes the shader resource group pool.
        ResultCode Init(Device& device, const ShaderResourcePoolDescriptor& descriptor);

        //! Initializes the resource group and associates it with the pool. The resource
        //! group must be updated on this pool.
        ResultCode InitShaderResource(ShaderResource& shaderResource);

        ResultCode CompileShaderReource(ShaderResource& shaderResource);

        ResultCode CompileShaderReourceBuffer(ShaderResource& shaderResource);

        ResultCode CompileShaderReourceImage(ShaderResource& shaderResource);

        ResultCode CompileShaderReourceConstantData(ShaderResource& shaderResource);

        ResultCode CompileShaderReourceSampler(ShaderResource& shaderResource);

        //! Returns the descriptor passed at initialization time.
        const ShaderResourcePoolDescriptor& GetDescriptor() const override;

        //! Returns the SRG layout used when initializing the pool.
        const ShaderResourceLayout* GetLayout() const;

        //! Begins compilation of the pool. Cannot be called recursively.
        //! @param groupsToCompileCount if non-null, is assigned to the number of groups needing to be compiled.
        void CompileShaderReourceBegin();

        //! Ends compilation of the pool. Must be preceeded by a CompileShaderReourceBegin() call.
        void CompileShaderReourceEnd();

        //////////////////////////////////////////////////////////////////////////
        // The following methods must be called within a CompileShaderReource{Begin, End} region.

        //! Returns the total number of groups that need to be compiled.
        uint32_t GetGroupsToCompileCount() const;

        //////////////////////////////////////////////////////////////////////////
        //! Returns whether layout in this pool has constants.
        bool HasConstants() const;

        //! Returns whether groups in this pool have an image table.
        bool HasImageGroup() const;

        //! Returns whether groups in this pool have a buffer table.
        bool HasBufferGroup() const;

        //! Returns whether groups in this pool have a sampler table.
        bool HasSamplerGroup() const;

    protected:
        ShaderResourcePool() = default;

        //////////////////////////////////////////////////////////////////////////
        // ResourcePool overrides
        void ShutdownResourceInternal(Resource& resource) override;
        //////////////////////////////////////////////////////////////////////////
    private:
        // Queues the shader resource group for compile. Legal to call on a queued group. Takes a lock.
        void QueueForCompile(ShaderResource& shaderResource);

        // Queues the shader resource group for compile. Legal to call on a queued group. Does NOT take a lock.
        void QueueForCompileNoLock(ShaderResource& shaderResource);

        // Un-queues the shader resource group for compile. Legal to call on an un-queued group. Takes a lock.
        void UnqueueForCompile(ShaderResource& shaderResource);

        //////////////////////////////////////////////////////////////////////////
        // Backend API

        // Called when the pool initializes.
        virtual ResultCode InitInternal(Device& device, const ShaderResourcePoolDescriptor& descriptor) = 0;

        // Initializes backing resources for the resource group.
        virtual ResultCode InitShaderResourceInternal(ShaderResource& shaderResourceGroup) = 0;

        virtual ResultCode CompileShaderReourceBufferInternal(ShaderResource& shaderResource) = 0;

        virtual ResultCode CompileShaderReourceImageInternal(ShaderResource& shaderResource) = 0;

        virtual ResultCode CompileShaderReourceConstantDataInternal(ShaderResource& shaderResource) = 0;

        virtual ResultCode CompileShaderReourceSamplerInternal(ShaderResource& shaderResource) = 0;

        //////////////////////////////////////////////////////////////////////////

        ShaderResourcePoolDescriptor m_descriptor;
        bool m_hasConstants = false;
        bool m_hasBufferGroup = false;
        bool m_hasImageGroup = false;
        bool m_hasSamplerGroup = false;
        bool m_isCompiling = false;

        mutable std::shared_mutex m_compileMutex;
        std::vector<ShaderResource*> m_shaderResourcesToCompile;
    };
}