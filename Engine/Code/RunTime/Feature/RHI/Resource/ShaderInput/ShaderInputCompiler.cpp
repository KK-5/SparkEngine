#include "ShaderInputCompiler.h"

#include <Log/ILogSystem.h>

#include <RHI/ValidationLayer.h>

namespace Spark::RHI
{
    ResultCode ShaderInputCompiler::Init(Device& device, const ShaderInputCompilerDescriptor& desc)
    {
        if (Validation::isEnabled)
        {
            if (IsInitialized())
            {
                LOG_ERROR("[ShaderInputCompiler] Already initialized.");
                return ResultCode::InvalidOperation;
            }
        }

        m_desc = desc;

        if (InitInternal(device, desc) == ResultCode::Success)
        {
            DeviceObject::Init(device);
            return ResultCode::Success;
        }

        return ResultCode::Fail;
    }

    void ShaderInputCompiler::Shutdown()
    {
        if (Validation::isEnabled)
        {
            if (!IsInitialized())
            {
                LOG_ERROR("[ShaderInputCompiler] Shutdown called on uninitialized instance.");
                return;
            }
        }
        DeviceObject::Shutdown();
    }

    ResultCode ShaderInputCompiler::Compile(ShaderBindings& bindings)
    {
        if (Validation::isEnabled)
        {
            if (!ValidateShaderBindings(bindings))
            {
                return ResultCode::InvalidOperation;
            }
        }
        return CompileInternal(bindings);
    }

    bool ShaderInputCompiler::ValidateShaderBindings(const ShaderBindings& bindings) const
    {
        if (!bindings.IsInitialized())
        {
            LOG_ERROR("[ShaderInputCompiler] Attempt to compile an uninitialized ShaderBindings.");
            return false;
        }
        return true;
    }
}
