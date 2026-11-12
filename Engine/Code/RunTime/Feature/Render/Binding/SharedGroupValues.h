#pragma once

#include <string.h>

#include <EASTL/fixed_vector.h>
#include <EASTL/type_traits.h>

#include <Pass/Component/ScopeComponents.h>

namespace Spark::Render
{
    //! The inputs of one shared group that no resource carries: samplers and constants. The
    //! group's owner refills them every frame, and the compiler writes them into the group
    //! object, so no system writes a ShaderBindings itself.
    struct SharedGroupValues
    {
        //! The group's space. How the compiler finds the object, wherever it lives.
        uint32_t m_spaceId = 0;

        eastl::fixed_vector<ScopeSampler, 8>   m_samplers;
        eastl::fixed_vector<ScopeConstant, 16> m_constants;
    };

    inline void PushSharedSampler(
        SharedGroupValues& values, RHI::InputName input, const RHI::SamplerState& state)
    {
        values.m_samplers.push_back(ScopeSampler{ input, state });
    }

    //! Stage `value`'s bytes as the constant `input`.
    template<typename T>
    void PushSharedConstant(SharedGroupValues& values, RHI::InputName input, const T& value)
    {
        static_assert(eastl::is_trivially_copyable_v<T>, "A constant is copied as bytes.");
        static_assert(sizeof(T) <= ScopeConstant::ByteCountMax,
            "A shared constant holds at most ScopeConstant::ByteCountMax bytes.");

        ScopeConstant& constant = values.m_constants.push_back();
        constant.m_input        = input;
        constant.m_byteCount    = static_cast<uint32_t>(sizeof(T));
        memcpy(constant.m_bytes.data(), &value, sizeof(T));
    }
}
