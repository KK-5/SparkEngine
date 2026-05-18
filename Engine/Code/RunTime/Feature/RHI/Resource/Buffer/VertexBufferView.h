#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/Base.h>
#include <RHI/RHILimits.h>
#include "VertexInputView.h"

namespace Spark::RHI
{
    class Buffer;

    struct VertexInput
    {
        VertexInputView m_vertexInputView;
        uint32_t m_inputSlot;
    };

    class alignas(8) VertexBufferView final
    {
    public:
        VertexBufferView() = default;

        void AddVertexInputView(const VertexInputView& vertexInputView);

        void SetVertexInputView(uint32_t slot, const VertexInputView& vertexInputView);

        const eastl::fixed_vector<VertexInput, Limits::Pipeline::StreamCountMax>& GetVertexInputs() const;

    private:
        bool ValidateVertexInputs() const;

        eastl::fixed_vector<VertexInput, Limits::Pipeline::StreamCountMax> m_vertexInputs;
    };
}