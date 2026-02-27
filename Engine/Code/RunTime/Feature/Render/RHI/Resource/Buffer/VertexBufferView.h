#pragma once

#include <EASTL/fixed_vector.h>

#include <RHI/Base.h>
#include <RHI/RHILimits.h>
#include "StreamBufferView.h"

namespace Spark::RHI
{
    class Buffer;

    struct VertexInput
    {
        StreamBufferView m_streamBufferView;
        uint32_t m_inputSlot;
    };

    class alignas(8) VertexBufferView final
    {
    public:
        VertexBufferView() = default;

        void AddStreamBufferView(const StreamBufferView& streamBufferView);

        void SetStreamBufferView(uint32_t slot, const StreamBufferView& streamBufferView);

        const eastl::fixed_vector<VertexInput, Limits::Pipeline::StreamCountMax>& GetVertexInputs() const;

    private:
        bool ValidateVertexInputs() const;

        eastl::fixed_vector<VertexInput, Limits::Pipeline::StreamCountMax> m_vertexInputs;
    };
}