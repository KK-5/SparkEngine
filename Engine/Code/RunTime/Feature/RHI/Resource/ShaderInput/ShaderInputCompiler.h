#pragma once

#include <RHI/Device/DeviceObject.h>

#include "ShaderBindings.h"

namespace Spark::RHI
{
    struct ShaderInputCompilerDescriptor
    {
        // currently empty
    };

    /// Compiles one ShaderBindings into backend-specific GPU resources (descriptor
    /// tables, constant buffer writes). Mirrors the lifecycle of the old
    /// ShaderResourceCompiler — one singleton instance per device, acquired via
    /// the factory.
    ///
    /// Compile is designed to be invoked **per-ShaderBindings, possibly from multiple
    /// threads in parallel**. The caller (typically the render layer's pass compiler
    /// or material system) decides on parallelism via its job system; this class does
    /// not batch internally.
    ///
    /// Backend implementations must ensure that concurrent Compile calls touching
    /// different ShaderBindings are safe — see DescriptorContext / ConstantBufferContext
    /// / Sampler factory threading guarantees.
    class ShaderInputCompiler : public DeviceObject
    {
    public:
        virtual ~ShaderInputCompiler() = default;

        ResultCode Init(Device& device, const ShaderInputCompilerDescriptor& desc);
        void       Shutdown() override;

        const ShaderInputCompilerDescriptor& GetDescriptor() const { return m_desc; }

        ResultCode Compile(ShaderBindings& bindings);

    protected:
        ShaderInputCompiler() = default;

    private:
        bool ValidateShaderBindings(const ShaderBindings& bindings) const;

        virtual ResultCode InitInternal(Device& device, const ShaderInputCompilerDescriptor& desc) = 0;
        virtual ResultCode CompileInternal(ShaderBindings& bindings) = 0;

        ShaderInputCompilerDescriptor m_desc;
    };
}
