#pragma once

#include <EASTL/span.h>

#include <RHI/Device/DeviceObject.h>
#include "ShaderResourceCompilerDescriptor.h"
#include "ShaderResource.h"

namespace Spark::RHI
{
    class ShaderResourceCompiler : public RHI::DeviceObject
    {
    public:
        virtual ~ShaderResourceCompiler() = default;

        ResultCode Init(RHI::Device& device, const RHI::ShaderResourceCompilerDescriptor& desc);
        void Shutdown() override;

        const ShaderResourceCompilerDescriptor& GetDescriptor() const;

        ResultCode Compiler(eastl::span<ShaderResource*> shaderResources);
    
    protected:
        ShaderResourceCompiler() = default;

    private:
        bool ValidateShaderResource(const ShaderResource& shaderResource);

        virtual ResultCode InitInternal(RHI::Device& device, const RHI::ShaderResourceCompilerDescriptor& desc) = 0;
        virtual ResultCode CompilerInternal(eastl::span<ShaderResource*> shaderResources) = 0;

        ShaderResourceCompilerDescriptor m_desc;
    };
}