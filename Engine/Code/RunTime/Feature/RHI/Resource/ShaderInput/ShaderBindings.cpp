#include "ShaderBindings.h"

#include <Log/ILogSystem.h>

namespace Spark::RHI
{
    ResultCode ShaderBindings::Init(Device& device, const Descriptor& descriptor)
    {
        if (!descriptor.m_layout || !descriptor.m_layout->IsFinalized())
        {
            LOG_ERROR("[ShaderBindings] Init: layout descriptor is null or not finalized.");
            return ResultCode::InvalidArgument;
        }

        if (descriptor.m_layout->FindSpaceGroupBySpaceId(descriptor.m_spaceId) == nullptr)
        {
            LOG_ERROR("[ShaderBindings] Init: layout has no SpaceGroup with spaceId=%u.",
                descriptor.m_spaceId);
            return ResultCode::InvalidArgument;
        }

        DeviceObject::Init(device);
        m_layoutDescriptor = descriptor.m_layout;
        m_spaceId          = descriptor.m_spaceId;

        BuildInputsFromLayout(descriptor);

        return InitInternal(device, descriptor);
    }

    void ShaderBindings::Shutdown()
    {
        ShutdownInternal();
        m_buffers.clear();
        m_images.clear();
        m_samplers.clear();
        m_constants.clear();
        m_layoutDescriptor = nullptr;
        DeviceObject::Shutdown();
    }

    void ShaderBindings::BuildInputsFromLayout(const Descriptor& descriptor)
    {
        const ShaderInputGroup& group = *descriptor.m_layout->FindSpaceGroupBySpaceId(descriptor.m_spaceId);

        for (const ShaderInputHandle& handle : group.m_shaderInputs)
        {
            switch (handle.m_type)
            {
            case ShaderInputType::Buffer:
            {
                m_buffers.push_back(ShaderInputBuffer(
                    descriptor.m_layout->GetBufferDescriptor(handle.m_index)));
                break;
            }
            case ShaderInputType::Image:
            {
                m_images.push_back(ShaderInputImage(
                    descriptor.m_layout->GetImageDescriptor(handle.m_index)));
                break;
            }
            case ShaderInputType::Sampler:
            {
                m_samplers.push_back(ShaderInputSampler(
                    descriptor.m_layout->GetSamplerDescriptor(handle.m_index)));
                break;
            }
            case ShaderInputType::Constant:
            {
                m_constants.push_back(ShaderInputConstant(
                    descriptor.m_layout->GetConstantDescriptor(handle.m_index)));
                break;
            }
            default:
                break;
            }
        }
    }

    ShaderInputBuffer* ShaderBindings::FindBufferInput(const InputName& name)
    {
        for (ShaderInputBuffer& input : m_buffers)
        {
            if (input.GetDescription().m_name == name)
            {
                return &input;
            }
        }
        return nullptr;
    }

    ShaderInputImage* ShaderBindings::FindImageInput(const InputName& name)
    {
        for (ShaderInputImage& input : m_images)
        {
            if (input.GetDescription().m_name == name)
            {
                return &input;
            }
        }
        return nullptr;
    }

    ShaderInputSampler* ShaderBindings::FindSamplerInput(const InputName& name)
    {
        for (ShaderInputSampler& input : m_samplers)
        {
            if (input.GetDescription().m_name == name)
            {
                return &input;
            }
        }
        return nullptr;
    }

    ShaderInputConstant* ShaderBindings::FindConstantInput(const InputName& name)
    {
        for (ShaderInputConstant& input : m_constants)
        {
            if (input.GetDescription().m_name == name)
            {
                return &input;
            }
        }
        return nullptr;
    }

    const ShaderInputBuffer* ShaderBindings::FindBufferInput(const InputName& name) const
    {
        for (const ShaderInputBuffer& input : m_buffers)
        {
            if (input.GetDescription().m_name == name)
            {
                return &input;
            }
        }
        return nullptr;
    }

    const ShaderInputImage* ShaderBindings::FindImageInput(const InputName& name) const
    {
        for (const ShaderInputImage& input : m_images)
        {
            if (input.GetDescription().m_name == name)
            {
                return &input;
            }
        }
        return nullptr;
    }

    const ShaderInputSampler* ShaderBindings::FindSamplerInput(const InputName& name) const
    {
        for (const ShaderInputSampler& input : m_samplers)
        {
            if (input.GetDescription().m_name == name)
            {
                return &input;
            }
        }
        return nullptr;
    }

    const ShaderInputConstant* ShaderBindings::FindConstantInput(const InputName& name) const
    {
        for (const ShaderInputConstant& input : m_constants)
        {
            if (input.GetDescription().m_name == name)
            {
                return &input;
            }
        }
        return nullptr;
    }

} // namespace Spark::RHI
