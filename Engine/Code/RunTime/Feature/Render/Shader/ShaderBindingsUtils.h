#pragma once

#include <Log/ILogSystem.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/Sampler/SamplerState.h>

namespace Spark::Render
{
    //! Fill ShaderBindings objects directly. RenderGraph compiles the objects its
    //! Scopes use, so these setters need no dirty tracking. Entity overloads below
    //! are transitional adapters for systems that still publish binding entities.

    namespace Detail
    {
        inline RHI::ShaderBindings* ResolveShaderBindings(RHI::RHIContext& ctx, RHI::RHIHandle bindings)
        {
            auto* comp = ctx.TryGet<RHI::Components::ShaderBindings>(bindings);
            if (!comp || !comp->m_bindings)
            {
                LOG_ERROR("[ShaderBindingsUtils] Entity has no Components::ShaderBindings (or it is null).");
                return nullptr;
            }
            return comp->m_bindings.get();
        }

    }

    //! Stage a constant's bytes, packed as in C++, into the named constant input.
    inline void SetShaderConstantData(
        RHI::ShaderBindings& bindings, RHI::InputName input, const void* bytes, uint32_t byteCount)
    {
        auto* constant = bindings.FindConstantInput(input);
        if (!constant)
        {
            LOG_ERROR("[ShaderBindingsUtils] Constant input '{}' not found.", input.GetCStr());
            return;
        }
        if (!constant->SetData(bytes, byteCount))
        {
            LOG_ERROR("[ShaderBindingsUtils] SetData size mismatch for constant '{}'.", input.GetCStr());
            return;
        }
    }

    //! Stage a constant value — any trivially-copyable POD (matrix / vector /
    //! scalar) — into the named constant input.
    template<typename T>
    void SetShaderConstant(RHI::ShaderBindings& bindings, RHI::InputName input, const T& value)
    {
        SetShaderConstantData(bindings, input, &value, static_cast<uint32_t>(sizeof(T)));
    }

    //! Bind an image view into the named image input.
    inline void SetShaderImage(
        RHI::ShaderBindings& bindings, RHI::InputName input, const RHI::ImageView* view, uint32_t arrayIndex = 0)
    {
        auto* image = bindings.FindImageInput(input);
        if (!image)
        {
            LOG_ERROR("[ShaderBindingsUtils] Image input '{}' not found.", input.GetCStr());
            return;
        }
        image->SetView(arrayIndex, view);
    }

    //! Bind a buffer view into the named buffer input.
    inline void SetShaderBuffer(
        RHI::ShaderBindings& bindings, RHI::InputName input, const RHI::BufferView* view, uint32_t arrayIndex = 0)
    {
        auto* buffer = bindings.FindBufferInput(input);
        if (!buffer)
        {
            LOG_ERROR("[ShaderBindingsUtils] Buffer input '{}' not found.", input.GetCStr());
            return;
        }
        buffer->SetView(arrayIndex, view);
    }

    inline void SetShaderAccelerationStructure(
        RHI::ShaderBindings& bindings, RHI::InputName input, const RHI::AccelerationStructure* structure, uint32_t arrayIndex = 0)
    {
        auto* target = bindings.FindAccelerationStructureInput(input);
        if (!target)
        {
            LOG_ERROR("[ShaderBindingsUtils] Acceleration structure input '{}' not found.", input.GetCStr());
            return;
        }
        if (arrayIndex >= target->GetCount())
        {
            LOG_ERROR("[ShaderBindingsUtils] Array index {} is out of range for acceleration structure input '{}'.",
                arrayIndex, input.GetCStr());
            return;
        }
        if (target->GetAccelerationStructure(arrayIndex).get() == structure)
        {
            return;
        }
        target->SetAccelerationStructure(arrayIndex, structure);
    }

    //! Bind a sampler state into the named sampler input.
    inline void SetShaderSampler(
        RHI::ShaderBindings& bindings, RHI::InputName input, const RHI::SamplerState& state, uint32_t arrayIndex = 0)
    {
        auto* sampler = bindings.FindSamplerInput(input);
        if (!sampler)
        {
            LOG_ERROR("[ShaderBindingsUtils] Sampler input '{}' not found.", input.GetCStr());
            return;
        }
        sampler->SetState(arrayIndex, state);
    }

    inline void SetShaderConstantData(
        RHI::RHIHandle entity, RHI::InputName input, const void* bytes, uint32_t byteCount)
    {
        if (auto* bindings = Detail::ResolveShaderBindings(*RHI::RHIExecuteContext::Current(), entity))
        {
            SetShaderConstantData(*bindings, input, bytes, byteCount);
        }
    }

    template<typename T>
    void SetShaderConstant(RHI::RHIHandle entity, RHI::InputName input, const T& value)
    {
        SetShaderConstantData(entity, input, &value, static_cast<uint32_t>(sizeof(T)));
    }

    inline void SetShaderImage(
        RHI::RHIHandle entity, RHI::InputName input, const RHI::ImageView* view, uint32_t arrayIndex = 0)
    {
        if (auto* bindings = Detail::ResolveShaderBindings(*RHI::RHIExecuteContext::Current(), entity))
        {
            SetShaderImage(*bindings, input, view, arrayIndex);
        }
    }

    inline void SetShaderBuffer(
        RHI::RHIHandle entity, RHI::InputName input, const RHI::BufferView* view, uint32_t arrayIndex = 0)
    {
        if (auto* bindings = Detail::ResolveShaderBindings(*RHI::RHIExecuteContext::Current(), entity))
        {
            SetShaderBuffer(*bindings, input, view, arrayIndex);
        }
    }

    inline void SetShaderAccelerationStructure(
        RHI::RHIHandle entity, RHI::InputName input, const RHI::AccelerationStructure* structure, uint32_t arrayIndex = 0)
    {
        if (auto* bindings = Detail::ResolveShaderBindings(*RHI::RHIExecuteContext::Current(), entity))
        {
            SetShaderAccelerationStructure(*bindings, input, structure, arrayIndex);
        }
    }

    inline void SetShaderSampler(
        RHI::RHIHandle entity, RHI::InputName input, const RHI::SamplerState& state, uint32_t arrayIndex = 0)
    {
        if (auto* bindings = Detail::ResolveShaderBindings(*RHI::RHIExecuteContext::Current(), entity))
        {
            SetShaderSampler(*bindings, input, state, arrayIndex);
        }
    }
}
