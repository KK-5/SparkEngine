/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ShaderResourcePool.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    ResultCode ShaderResourcePool::Init(Device& device, const ShaderResourcePoolDescriptor& descriptor)
    {
        if (Validation::isEnabled)
        {
            if (descriptor.m_layout == nullptr)
            {
                LOG_ERROR("[ShaderResourcePool] ShaderResourceGroupPoolDescriptor::m_layout must not be null.");
                return ResultCode::InvalidArgument;
            }
        }

        ResultCode resultCode = ResourcePool::Init(
            device, descriptor,
            [this, &device, &descriptor]()
        {
            return InitInternal(device, descriptor);
        });

        if (resultCode != ResultCode::Success)
        {
            return resultCode;
        }


        m_descriptor = descriptor;
        m_hasBufferGroup = m_descriptor.m_layout->GetBuffersSize() > 0;
        m_hasImageGroup = m_descriptor.m_layout->GetImagesSize() > 0;
        m_hasSamplerGroup = m_descriptor.m_layout->GetSamplersSize() > 0;
        m_hasConstants = m_descriptor.m_layout->GetConstantDataSize() > 0;

        return ResultCode::Success;
    }

    ResultCode ShaderResourcePool::InitShaderResource(ShaderResource& shaderResource)
    {
        ResultCode resultCode = ResourcePool::InitResource(&shaderResource, [this, &shaderResource]() { return InitShaderResourceInternal(shaderResource); });
        if (resultCode == ResultCode::Success)
        {
            const ShaderResourceLayout* layout = GetLayout();

            // Pre-initialize the data so that we can build view diffs later.
            shaderResource.SetLayout(layout);

            // Cache off the binding slot for one less indirection.
            shaderResource.m_bindingSlot = layout->GetBindingSlot();
        }
        return resultCode;
    }

    void ShaderResourcePool::ShutdownResourceInternal(Resource& resourceBase)
    {
        ShaderResource& shaderResource = static_cast<ShaderResource&>(resourceBase);

        UnqueueForCompile(shaderResource);

        shaderResource.SetLayout(nullptr);
    }

    void ShaderResourcePool::QueueForCompile(ShaderResource& shaderResource)
    {
        std::lock_guard<std::shared_mutex> lock(m_compileMutex);
        QueueForCompileNoLock(shaderResource);
    }

    void ShaderResourcePool::QueueForCompileNoLock(ShaderResource& shaderResource)
    {
        m_shaderResourcesToCompile.emplace_back(&shaderResource);
    }

    void ShaderResourcePool::UnqueueForCompile(ShaderResource& shaderResource)
    {
        std::lock_guard<std::shared_mutex> lock(m_compileMutex);
        m_shaderResourcesToCompile.erase(eastl::find(m_shaderResourcesToCompile.begin(), m_shaderResourcesToCompile.end(), &shaderResource));
    }

    void ShaderResourcePool::CompileShaderReourceBegin()
    {
        ASSERT(m_isCompiling == false, "Already compiling! Deadlock imminent.");
        m_compileMutex.lock();
        m_isCompiling = true;
    }

    void ShaderResourcePool::CompileShaderReourceEnd()
    {
        ASSERT(m_isCompiling, "CompileShaderReourceBegin() was never called.");
        m_isCompiling = false;
        m_shaderResourcesToCompile.clear();
        m_compileMutex.unlock();
    }

    uint32_t ShaderResourcePool::GetGroupsToCompileCount() const
    {
        ASSERT(m_isCompiling, "You must call this function within a CompileShaderReource{Begin, End} region!");
        return static_cast<uint32_t>(m_shaderResourcesToCompile.size());
    }

    ResultCode ShaderResourcePool::CompileShaderReource(ShaderResource& shaderResource)
    {
        auto CheckResult = [](ResultCode result)
        {
            if (result != ResultCode::Success)
            {
                ASSERT(false, "[ShaderResourcePool] Compile Shader Resource Failed!");
            }
        };
        CheckResult(CompileShaderReourceBuffer(shaderResource));
        CheckResult(CompileShaderReourceImage(shaderResource));
        CheckResult(CompileShaderReourceConstantData(shaderResource));
        CheckResult(CompileShaderReourceSampler(shaderResource));
        return ResultCode::Success;
    }

    ResultCode ShaderResourcePool::CompileShaderReourceBuffer(ShaderResource& shaderResource)
    {
        if (!HasBufferGroup())
        {
            LOG_ERROR("[ShaderResourcePool] Attempt to compile a shader resource that does not have buffer view.");
            return ResultCode::InvalidOperation;
        }

        return CompileShaderReourceBufferInternal(shaderResource);
    }

    ResultCode ShaderResourcePool::CompileShaderReourceImage(ShaderResource& shaderResource)
    {
        if (!HasImageGroup())
        {
            LOG_ERROR("[ShaderResourcePool] Attempt to compile a shader resource that does not have image view.");
            return ResultCode::InvalidOperation;
        }

        return CompileShaderReourceImageInternal(shaderResource);
    }

    ResultCode ShaderResourcePool::CompileShaderReourceConstantData(ShaderResource& shaderResource)
    {
        if (!HasConstants())
        {
            LOG_ERROR("[ShaderResourcePool] Attempt to compile a shader resource that does not have constant data.");
            return ResultCode::InvalidOperation;
        }

        return CompileShaderReourceConstantDataInternal(shaderResource);
    }

    ResultCode ShaderResourcePool::CompileShaderReourceSampler(ShaderResource& shaderResource)
    {
        if (!HasSamplerGroup())
        {
            LOG_ERROR("[ShaderResourcePool] Attempt to compile a shader resource that does not have sampler.");
            return ResultCode::InvalidOperation;
        }

        return CompileShaderReourceSamplerInternal(shaderResource);
    }

    const ShaderResourceLayout* ShaderResourcePool::GetLayout() const
    {
        ASSERT(m_descriptor.m_layout, "Shader resource group layout is null");
        return m_descriptor.m_layout.get();
    }

    bool ShaderResourcePool::HasConstants() const
    {
        return m_hasConstants;
    }

    bool ShaderResourcePool::HasBufferGroup() const
    {
        return m_hasBufferGroup;
    }

    bool ShaderResourcePool::HasImageGroup() const
    {
        return m_hasImageGroup;
    }

    bool ShaderResourcePool::HasSamplerGroup() const
    {
        return m_hasSamplerGroup;
    }
}