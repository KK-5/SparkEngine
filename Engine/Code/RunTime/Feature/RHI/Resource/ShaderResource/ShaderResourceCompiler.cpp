#include "ShaderResourceCompiler.h"

namespace Spark::RHI
{
    ResultCode ShaderResourceCompiler::Init(RHI::Device& device, const RHI::ShaderResourceCompilerDescriptor& desc)
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_ERROR("[ShaderResourceCompiler] ShaderResourceCompiler is alreay initialized.");
                return ResultCode::InvalidOperation;
            }
        }

        m_desc = desc;

        if(InitInternal(device, desc) == ResultCode::Success)
        {
            DeviceObject::Init(device);
            return ResultCode::Success;
        }

        return ResultCode::Fail;
    }

    void ShaderResourceCompiler::Shutdown()
    {
        if (Validation::isEnabled)
        {
            if (!IsInitialized())
            {
                LOG_ERROR("[ShaderResourceCompiler] ShaderResourceCompiler is invalid.");
                return;
            }
        }

        DeviceObject::Shutdown();
    }

    const ShaderResourceCompilerDescriptor& ShaderResourceCompiler::GetDescriptor() const
    {
        return m_desc;
    }

    ResultCode ShaderResourceCompiler::Compiler(eastl::span<ShaderResource*> shaderResources)
    {
        if (Validation::isEnabled)
        {
            for(auto shaderResource: shaderResources)
            {
                if (!ValidateShaderResource(*shaderResource))
                {
                    return ResultCode::InvalidOperation;
                }
            }
        }

        return CompilerInternal(shaderResources);
    }

    bool ShaderResourceCompiler::ValidateShaderResource(const ShaderResource& shaderResource)
    {
        if (!shaderResource.IsInitialized())
        {
            LOG_ERROR("[ShaderResourceCompiler] Attempt to compile an uninitialized ShaderResource. ");
            return false;
        }

        return true;
    }

}