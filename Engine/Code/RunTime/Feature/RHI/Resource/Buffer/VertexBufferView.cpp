#include "VertexBufferView.h"

#include <Log/SpdLogSystem.h>

namespace Spark::RHI
{
    void VertexBufferView::AddVertexInputView(const VertexInputView& vertexInputView)
    {
        m_vertexInputs.emplace_back();

        VertexInput& input = m_vertexInputs.back();
        input.m_vertexInputView = vertexInputView;
        input.m_inputSlot = m_vertexInputs.size() - 1;

        ASSERT(ValidateVertexInputs(), "[VertexBufferView] AddVertexInputView failed!");
    }

    void VertexBufferView::SetVertexInputView(uint32_t slot, const VertexInputView& vertexInputView)
    {
        m_vertexInputs.emplace_back();

        VertexInput& input = m_vertexInputs.back();
        input.m_vertexInputView = vertexInputView;
        input.m_inputSlot = slot;

        ASSERT(ValidateVertexInputs(), "[VertexBufferView] SetVertexInputView failed!");
    }

    const eastl::fixed_vector<VertexInput, Limits::Pipeline::StreamCountMax>& VertexBufferView::GetVertexInputs() const
    {
        return m_vertexInputs;
    }

    bool VertexBufferView::ValidateVertexInputs() const
    {
        if (Validation::isEnabled)
        {
            uint32_t maxSlot = 0;
            eastl::array<bool, Limits::Pipeline::StreamCountMax> existSlot;
            existSlot.fill(false);
            for (auto& vertexInput: m_vertexInputs)
            {
                if (existSlot[vertexInput.m_inputSlot])
                {
                    LOG_ERROR("[VertexBufferView] Duplicate input slot");
                    return false;
                }
                existSlot[vertexInput.m_inputSlot] = true;

                maxSlot = maxSlot < vertexInput.m_inputSlot ? vertexInput.m_inputSlot : maxSlot;
            }

            if (maxSlot != m_vertexInputs.size() - 1)
            {
                LOG_ERROR("[VertexBufferView] Vertex inputs must start from slot 0 and remain continuous.");
                return false;
            }
            return true;
        }
        return true;
    }
}