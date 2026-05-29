#pragma once

#include <EASTL/fixed_vector.h>
#include <EASTL/vector.h>
#include <EASTL/span.h>

#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>

namespace Spark::RHI
{

    //! A ShaderInput is a self-describing binding point: it carries both the descriptor
    //! metadata (register, space, type, name) and the bound resource data (views, states, bytes).
    //! Each ShaderInput corresponds to a single variable or array declaration in the shader source.
    //!
    //! Unlike the older ShaderResource model, there is no separate layout object -- the ShaderInput
    //! owns its description and data together. A group of ShaderInput objects feeds into PipelineLayout
    //! (via SpaceGroup) and the ShaderInputCompiler produces ShaderBindings at frame compile time.

    //////////////////////////////////////////////////////////////////////////
    // ShaderInputBuffer
    //////////////////////////////////////////////////////////////////////////

    class ShaderInputBuffer
    {
    public:
        static constexpr uint32_t ArrayCountMax = 16;

        ShaderInputBuffer() = default;
        explicit ShaderInputBuffer(const ShaderInputBufferDescriptor& desc) { Init(desc); }

        void Init(const ShaderInputBufferDescriptor& desc)
        {
            m_descriptor = desc;
            m_views.resize(desc.m_count);
        }

        const ShaderInputBufferDescriptor& GetDescription() const { return m_descriptor; }

        bool SetView(uint32_t arrayIndex, const BufferView* view)
        {
            if (arrayIndex >= m_descriptor.m_count)
            {
                return false;
            }
            m_views[arrayIndex] = view;
            return true;
        }

        bool SetViews(eastl::span<const BufferView*> views, uint32_t arrayIndex = 0)
        {
            if (arrayIndex + static_cast<uint32_t>(views.size()) > m_descriptor.m_count)
            {
                return false;
            }
            for (uint32_t i = 0; i < static_cast<uint32_t>(views.size()); ++i)
            {
                m_views[arrayIndex + i] = views[i];
            }
            return true;
        }

        const ConstPtr<BufferView>& GetView(uint32_t arrayIndex) const
        {
            static const ConstPtr<BufferView> s_null;
            if (arrayIndex >= m_descriptor.m_count)
            {
                return s_null;
            }
            return m_views[arrayIndex];
        }

        eastl::span<const ConstPtr<BufferView>> GetViews() const
        {
            return { m_views.data(), m_views.size() };
        }

        uint32_t GetCount() const { return m_descriptor.m_count; }

    private:
        ShaderInputBufferDescriptor m_descriptor;
        eastl::fixed_vector<ConstPtr<BufferView>, ArrayCountMax> m_views;
    };

    //////////////////////////////////////////////////////////////////////////
    // ShaderInputImage
    //////////////////////////////////////////////////////////////////////////

    class ShaderInputImage
    {
    public:
        static constexpr uint32_t ArrayCountMax = 16;

        ShaderInputImage() = default;
        explicit ShaderInputImage(const ShaderInputImageDescriptor& desc) { Init(desc); }

        void Init(const ShaderInputImageDescriptor& desc)
        {
            m_descriptor = desc;
            m_views.resize(desc.m_count);
        }

        const ShaderInputImageDescriptor& GetDescription() const { return m_descriptor; }

        bool SetView(uint32_t arrayIndex, const ImageView* view)
        {
            if (arrayIndex >= m_descriptor.m_count)
            {
                return false;
            }
            m_views[arrayIndex] = view;
            return true;
        }

        bool SetViews(eastl::span<const ImageView*> views, uint32_t arrayIndex = 0)
        {
            if (arrayIndex + static_cast<uint32_t>(views.size()) > m_descriptor.m_count)
            {
                return false;
            }
            for (uint32_t i = 0; i < static_cast<uint32_t>(views.size()); ++i)
            {
                m_views[arrayIndex + i] = views[i];
            }
            return true;
        }

        const ConstPtr<ImageView>& GetView(uint32_t arrayIndex) const
        {
            static const ConstPtr<ImageView> s_null;
            if (arrayIndex >= m_descriptor.m_count)
            {
                return s_null;
            }
            return m_views[arrayIndex];
        }

        eastl::span<const ConstPtr<ImageView>> GetViews() const
        {
            return { m_views.data(), m_views.size() };
        }

        uint32_t GetCount() const { return m_descriptor.m_count; }

    private:
        ShaderInputImageDescriptor m_descriptor;
        eastl::fixed_vector<ConstPtr<ImageView>, ArrayCountMax> m_views;
    };

    //////////////////////////////////////////////////////////////////////////
    // ShaderInputSampler
    //////////////////////////////////////////////////////////////////////////

    class ShaderInputSampler
    {
    public:
        static constexpr uint32_t ArrayCountMax = 8;

        ShaderInputSampler() = default;
        explicit ShaderInputSampler(const ShaderInputSamplerDescriptor& desc) { Init(desc); }

        void Init(const ShaderInputSamplerDescriptor& desc)
        {
            m_descriptor = desc;
            m_states.resize(desc.m_count);
        }

        const ShaderInputSamplerDescriptor& GetDescription() const { return m_descriptor; }

        bool SetState(uint32_t arrayIndex, const SamplerState& state)
        {
            if (arrayIndex >= m_descriptor.m_count)
            {
                return false;
            }
            m_states[arrayIndex] = state;
            return true;
        }

        bool SetStates(eastl::span<const SamplerState> states, uint32_t arrayIndex = 0)
        {
            if (arrayIndex + static_cast<uint32_t>(states.size()) > m_descriptor.m_count)
            {
                return false;
            }
            for (uint32_t i = 0; i < static_cast<uint32_t>(states.size()); ++i)
            {
                m_states[arrayIndex + i] = states[i];
            }
            return true;
        }

        const SamplerState& GetState(uint32_t arrayIndex) const
        {
            static const SamplerState s_null;
            if (arrayIndex >= m_descriptor.m_count)
            {
                return s_null;
            }
            return m_states[arrayIndex];
        }

        eastl::span<const SamplerState> GetStates() const
        {
            return { m_states.data(), m_states.size() };
        }

        uint32_t GetCount() const { return m_descriptor.m_count; }

    private:
        ShaderInputSamplerDescriptor m_descriptor;
        eastl::fixed_vector<SamplerState, ArrayCountMax> m_states;
    };

    //////////////////////////////////////////////////////////////////////////
    // ShaderInputConstant
    //////////////////////////////////////////////////////////////////////////

    class ShaderInputConstant
    {
    public:
        ShaderInputConstant() = default;
        explicit ShaderInputConstant(const ShaderInputConstantDescriptor& desc) { Init(desc); }

        void Init(const ShaderInputConstantDescriptor& desc)
        {
            m_descriptor = desc;
            m_data.resize(desc.m_constantByteCount);
        }

        const ShaderInputConstantDescriptor& GetDescription() const { return m_descriptor; }

        bool SetData(const void* bytes, uint32_t byteCount)
        {
            if (byteCount != m_descriptor.m_constantByteCount)
            {
                return false;
            }
            memcpy(m_data.data(), bytes, byteCount);
            return true;
        }

        bool SetData(const void* bytes, uint32_t byteOffset, uint32_t byteCount)
        {
            if (byteOffset + byteCount > m_descriptor.m_constantByteCount)
            {
                return false;
            }
            memcpy(m_data.data() + byteOffset, bytes, byteCount);
            return true;
        }

        eastl::span<const uint8_t> GetData() const
        {
            return { m_data.data(), m_data.size() };
        }

        uint32_t GetByteCount() const { return m_descriptor.m_constantByteCount; }

    private:
        ShaderInputConstantDescriptor m_descriptor;
        eastl::vector<uint8_t> m_data;
    };

} // namespace Spark::RHI
