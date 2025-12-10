#include "ClearValue.h"

namespace Spark::Render::RHI
{
    ClearValue::ClearValue()
    {
        memset(this, 0, sizeof(ClearValue));
        m_type = ClearValueType::Vector4Float;
    }

    ClearValue ClearValue::CreateVector4Float(float x, float y, float z, float w)
    {
        ClearValue value;
        value.m_type = ClearValueType::Vector4Float;
        value.m_vector4Float[0] = x;
        value.m_vector4Float[1] = y;
        value.m_vector4Float[2] = z;
        value.m_vector4Float[3] = w;
        return value;
    }

    ClearValue ClearValue::CreateVector4Uint(uint32_t x, uint32_t y, uint32_t z, uint32_t w)
    {
        ClearValue value;
        value.m_type = ClearValueType::Vector4Uint;
        value.m_vector4Uint[0] = x;
        value.m_vector4Uint[1] = y;
        value.m_vector4Uint[2] = z;
        value.m_vector4Uint[3] = w;
        return value;
    }

    ClearValue ClearValue::CreateStencil(uint8_t stencil)
    {
        ClearValue value;
        value.m_type = ClearValueType::DepthStencil;
        value.m_depthStencil.m_depth = 0.0;
        value.m_depthStencil.m_stencil = stencil;
        return value;
    }

    ClearValue ClearValue::CreateDepth(float depth)
    {
        ClearValue value;
        value.m_type = ClearValueType::DepthStencil;
        value.m_depthStencil.m_depth = depth;
        value.m_depthStencil.m_stencil = 0;
        return value;
    }

    ClearValue ClearValue::CreateDepthStencil(float depth, uint8_t stencil)
    {
        ClearValue value;
        value.m_type = ClearValueType::DepthStencil;
        value.m_depthStencil.m_depth = depth;
        value.m_depthStencil.m_stencil = stencil;
        return value;
    }

    bool ClearValue::operator==(const ClearValue& other) const
    {
        return
            m_type == other.m_type &&
            m_depthStencil == other.m_depthStencil &&
            m_vector4Uint == other.m_vector4Uint &&
            abs(m_vector4Float[0] - other.m_vector4Float[0]) < eastl::numeric_limits<float>::epsilon() &&
            abs(m_vector4Float[1] - other.m_vector4Float[1]) < eastl::numeric_limits<float>::epsilon() &&
            abs(m_vector4Float[2] - other.m_vector4Float[2]) < eastl::numeric_limits<float>::epsilon() &&
            abs(m_vector4Float[3] - other.m_vector4Float[3]) < eastl::numeric_limits<float>::epsilon();
    }
}