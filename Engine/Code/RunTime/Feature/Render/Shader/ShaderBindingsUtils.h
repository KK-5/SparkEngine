#pragma once

#include <Log/ILogSystem.h>

#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/Sampler/SamplerState.h>

namespace Spark::Render
{
    //! Fill a ShaderBindings object. The render graph compiles the ones its Scopes use, and a
    //! setter that writes what is already there leaves the object clean, so compile skips it.

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
        if (constant->IsSameData(bytes, byteCount))
        {
            return;
        }
        if (!constant->SetData(bytes, byteCount))
        {
            LOG_ERROR("[ShaderBindingsUtils] SetData size mismatch for constant '{}'.", input.GetCStr());
            return;
        }
        bindings.MarkDirty();
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
        if (image->GetView(arrayIndex).get() == view)
        {
            return;
        }
        image->SetView(arrayIndex, view);
        bindings.MarkDirty();
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
        if (buffer->GetView(arrayIndex).get() == view)
        {
            return;
        }
        buffer->SetView(arrayIndex, view);
        bindings.MarkDirty();
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
        bindings.MarkDirty();
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
        if (sampler->GetState(arrayIndex) == state)
        {
            return;
        }
        sampler->SetState(arrayIndex, state);
        bindings.MarkDirty();
    }

}
