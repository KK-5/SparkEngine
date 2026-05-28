#include "ShaderBuilder.h"

#include <EASTL/set.h>
#include <Core/Service/Service.h>
#include <RHI/Factory.h>

namespace Spark::Render
{
    namespace
    {
        struct RegisterKey
        {
            uint32_t registerId;
            uint32_t spaceId;
            RHI::ShaderInputType type;

            bool operator<(const RegisterKey& other) const
            {
                if (registerId != other.registerId) return registerId < other.registerId;
                if (spaceId != other.spaceId) return spaceId < other.spaceId;
                return static_cast<uint32_t>(type) < static_cast<uint32_t>(other.type);
            }
        };
    }

    void BuildShaderResourceLayout(const Resource::ShaderAsset& shader, RHI::ShaderResourceLayout& layout)
    {
        auto* shaderData = shader.GetShaderData();
        if (!shaderData)
        {
            return;
        }

        eastl::set<RegisterKey> addedBindings;
        eastl::set<eastl::string> addedConstants;

        for (uint32_t stageIdx = 0; stageIdx < static_cast<uint32_t>(RHI::ShaderStage::Count); ++stageIdx)
        {
            auto stage = static_cast<RHI::ShaderStage>(stageIdx);
            auto* refl = shaderData->GetStageReflection(stage);
            if (!refl)
            {
                continue;
            }

            // cbuffer → 每个变量展开为 Constant 描述符（GPU 虚拟地址绑定）
            for (const auto& cb : refl->m_cbuffers)
            {
                for (const auto& var : cb.m_variables)
                {
                    if (addedConstants.insert(var.m_name).second)
                    {
                        layout.AddShaderInput(RHI::ShaderInputConstantDescriptor(
                            RHI::InputName(var.m_name.c_str()),
                            var.m_byteOffset,
                            var.m_byteSize,
                            cb.m_registerId,
                            cb.m_spaceId));
                    }
                }
            }

            // 资源绑定: SRV / UAV / Sampler
            for (const auto& res : refl->m_resources)
            {
                RegisterKey key{res.m_registerId, res.m_spaceId, res.m_type};
                if (!addedBindings.insert(key).second)
                {
                    continue;
                }

                switch (res.m_type)
                {
                case RHI::ShaderInputType::Buffer:
                {
                    uint32_t count = res.m_count > 0 ? res.m_count : 1;
                    layout.AddShaderInput(RHI::ShaderInputBufferDescriptor(
                        RHI::InputName(res.m_name.c_str()),
                        res.m_bufferAccess,
                        res.m_bufferType,
                        count, 0,
                        res.m_registerId,
                        res.m_spaceId));
                    break;
                }
                case RHI::ShaderInputType::Image:
                {
                    uint32_t count = res.m_count > 0 ? res.m_count : 1;
                    layout.AddShaderInput(RHI::ShaderInputImageDescriptor(
                        RHI::InputName(res.m_name.c_str()),
                        res.m_imageAccess,
                        res.m_imageType,
                        count,
                        res.m_registerId,
                        res.m_spaceId));
                    break;
                }
                case RHI::ShaderInputType::Sampler:
                {
                    uint32_t count = res.m_count > 0 ? res.m_count : 1;
                    layout.AddShaderInput(RHI::ShaderInputSamplerDescriptor(
                        RHI::InputName(res.m_name.c_str()),
                        count,
                        res.m_registerId,
                        res.m_spaceId));
                    break;
                }
                default:
                    break;
                }
            }
        }
    }

    Ptr<RHI::ShaderResourceLayout> BuildShaderResourceLayout(const Resource::ShaderAsset& shader)
    {
        auto* factory = Service<RHI::Factory>::Get();
        if (!factory)
        {
            return nullptr;
        }

        auto layout = factory->CreateShaderResourceLayout();
        BuildShaderResourceLayout(shader, *layout);
        layout->Finalize();
        return layout;
    }
}
