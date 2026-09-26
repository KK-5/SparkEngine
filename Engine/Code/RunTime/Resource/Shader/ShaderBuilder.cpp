#include "ShaderBuilder.h"

#include <EASTL/map.h>

#include <Log/ILogSystem.h>

namespace Spark::Resource
{
    namespace
    {
        //! D3D register classes (HLSL b/t/u/s) each have an INDEPENDENT index space, so
        //! the same registerId in different classes is a distinct binding. ShaderInputType
        //! alone cannot tell an SRV texture (t0) from a UAV texture (u0) — both are Image —
        //! so the dedup key must also fold in the class derived from the resource's access.
        enum class RegisterClass : uint8_t { CBV, SRV, UAV, Sampler };

        RegisterClass ResolveRegisterClass(const ShaderResourceBindingReflection& res)
        {
            switch (res.m_type)
            {
            case RHI::ShaderInputType::Sampler:
                return RegisterClass::Sampler;
            case RHI::ShaderInputType::Image:
                return res.m_imageAccess == RHI::ShaderInputImageAccess::ReadWrite
                    ? RegisterClass::UAV : RegisterClass::SRV;
            case RHI::ShaderInputType::Buffer:
                if (res.m_bufferAccess == RHI::ShaderInputBufferAccess::ReadWrite) return RegisterClass::UAV;
                if (res.m_bufferAccess == RHI::ShaderInputBufferAccess::Constant)  return RegisterClass::CBV;
                return RegisterClass::SRV;
            default:
                return RegisterClass::SRV;
            }
        }

        struct RegisterKey
        {
            uint32_t      registerId;
            uint32_t      spaceId;
            RegisterClass regClass;

            bool operator<(const RegisterKey& other) const
            {
                if (registerId != other.registerId) return registerId < other.registerId;
                if (spaceId != other.spaceId) return spaceId < other.spaceId;
                return static_cast<uint32_t>(regClass) < static_cast<uint32_t>(other.regClass);
            }
        };

        RHI::ShaderStageMask StageToMask(RHI::ShaderStage stage)
        {
            return static_cast<RHI::ShaderStageMask>(BIT(static_cast<uint32_t>(stage)));
        }

        //! 跨多次调用共享的去重表，配合 MergeStageReflection 使用。值是条目在输出 list 里的位置，
        //! 后续 stage 再遇到同一输入时把自己的 stage 或进去。
        struct ShaderInputMergeState
        {
            eastl::map<RegisterKey, RHI::ShaderInputHandle> addedBindings;
            eastl::map<eastl::string, uint32_t>             addedConstants;
            eastl::map<eastl::string, uint32_t>             addedRootConstants;
        };

        RHI::ShaderStageMask& StageMaskOf(RHI::ShaderInputList& list, const RHI::ShaderInputHandle& handle)
        {
            switch (handle.m_type)
            {
            case RHI::ShaderInputType::Buffer:
                return list.m_buffers[handle.m_index].m_stageMask;
            case RHI::ShaderInputType::Image:
                return list.m_images[handle.m_index].m_stageMask;
            default:
                return list.m_samplers[handle.m_index].m_stageMask;
            }
        }

        //! 将单个 stage 反射并入 ShaderInputList，去重表跨调用累积；每个条目的 m_stageMask 是引用它的
        //! stage 的并集。Buffer/Image/Sampler 按 (spaceId, registerId, type) 去重；
        //! 常量按变量名去重（cbuffer 变量展开为独立 ShaderInputConstantDescriptor，
        //! Finalize 时 PipelineLayoutDescriptor 再按 registerId 聚合成 ConstantBufferLayout）。
        void MergeStageReflection(
            const ShaderStageReflection& refl,
            RHI::ShaderStageMask         stageMask,
            ShaderInputMergeState&       state,
            RHI::ShaderInputList&        out)
        {
            for (const auto& cb : refl.m_cbuffers)
            {
                const bool isRoot = cb.m_spaceId == RootConstantsSpaceId;
                auto& constants   = isRoot ? out.m_rootConstants : out.m_constants;
                auto& added       = isRoot ? state.addedRootConstants : state.addedConstants;
                const auto& other = isRoot ? state.addedConstants : state.addedRootConstants;

                for (const auto& var : cb.m_variables)
                {
                    // Bindings look a name up in both: it must belong to one.
                    ASSERT(other.find(var.m_name) == other.end(),
                        "[ShaderBuilder] '{}' names both a root constant and a cbuffer constant.", var.m_name.c_str());

                    const auto [it, inserted] = added.insert(
                        { var.m_name, static_cast<uint32_t>(constants.size()) });
                    if (!inserted)
                    {
                        constants[it->second].m_stageMask |= stageMask;
                        continue;
                    }
                    RHI::ShaderInputConstantDescriptor desc(
                        RHI::InputName(var.m_name.c_str()),
                        var.m_byteOffset,
                        var.m_byteSize,
                        var.m_elementCount,
                        var.m_elementByteSize,
                        var.m_elementStride,
                        cb.m_registerId,
                        cb.m_spaceId);
                    desc.m_stageMask = stageMask;
                    constants.push_back(desc);
                }
            }

            for (const auto& res : refl.m_resources)
            {
                RegisterKey key{ res.m_registerId, res.m_spaceId, ResolveRegisterClass(res) };
                const auto existing = state.addedBindings.find(key);
                if (existing != state.addedBindings.end())
                {
                    StageMaskOf(out, existing->second) |= stageMask;
                    continue;
                }

                const uint32_t count = res.m_count > 0 ? res.m_count : 1;

                switch (res.m_type)
                {
                case RHI::ShaderInputType::Buffer:
                {
                    RHI::ShaderInputBufferDescriptor desc(
                        RHI::InputName(res.m_name.c_str()),
                        res.m_bufferAccess,
                        res.m_bufferType,
                        count, 0,
                        res.m_registerId,
                        res.m_spaceId);
                    desc.m_stageMask = stageMask;
                    state.addedBindings[key] = { res.m_type, static_cast<uint32_t>(out.m_buffers.size()) };
                    out.m_buffers.push_back(desc);
                    break;
                }
                case RHI::ShaderInputType::Image:
                {
                    RHI::ShaderInputImageDescriptor desc(
                        RHI::InputName(res.m_name.c_str()),
                        res.m_imageAccess,
                        res.m_imageType,
                        count,
                        res.m_registerId,
                        res.m_spaceId);
                    desc.m_stageMask = stageMask;
                    state.addedBindings[key] = { res.m_type, static_cast<uint32_t>(out.m_images.size()) };
                    out.m_images.push_back(desc);
                    break;
                }
                case RHI::ShaderInputType::Sampler:
                {
                    RHI::ShaderInputSamplerDescriptor desc(
                        RHI::InputName(res.m_name.c_str()),
                        count,
                        res.m_registerId,
                        res.m_spaceId);
                    desc.m_stageMask = stageMask;
                    state.addedBindings[key] = { res.m_type, static_cast<uint32_t>(out.m_samplers.size()) };
                    out.m_samplers.push_back(desc);
                    break;
                }
                default:
                    break;
                }
            }
        }

        //! 单个 ShaderAsset 的反射遍历主体：迭代所有 stage，并入 list、累积 stageMask。
        //! stageOwner != nullptr 时启用跨 asset 的 stage 唯一性检查（多 asset 路径用）。
        void MergeAssetReflection(
            const ShaderAsset&        shader,
            ShaderInputMergeState&    state,
            ShaderInputBuildResult&   result,
            const ShaderAsset**       stageOwner /* nullable, size = ShaderStageCount */)
        {
            auto* shaderData = shader.GetShaderData();
            if (!shaderData)
            {
                return;
            }

            for (uint32_t i = 0; i < static_cast<uint32_t>(RHI::ShaderStage::Count); ++i)
            {
                auto  stage = static_cast<RHI::ShaderStage>(i);
                auto* refl  = shaderData->GetStageReflection(stage);
                if (!refl)
                {
                    continue;
                }

                if (stageOwner != nullptr)
                {
                    ASSERT(stageOwner[i] == nullptr,
                        "[BuildShaderInputList] ShaderStage %u is contributed by more than one "
                        "ShaderAsset; a pass cannot have multiple shaders for the same stage.", i);
                    stageOwner[i] = &shader;
                }

                result.stageMask = result.stageMask | StageToMask(stage);
                MergeStageReflection(*refl, StageToMask(stage), state, result.list);
            }
        }
    }

    ShaderInputBuildResult BuildShaderInputList(const ShaderAsset& shader)
    {
        ShaderInputBuildResult result;
        ShaderInputMergeState  state;
        MergeAssetReflection(shader, state, result, /*stageOwner*/nullptr);
        return result;
    }

    ShaderInputBuildResult BuildShaderInputList(
        eastl::span<const ShaderAsset* const> shaders)
    {
        ShaderInputBuildResult result;
        ShaderInputMergeState  state;

        // 指针去重 — 同一个组合 HLSL 被同时挂到多个 stage 槽位时只处理一次。
        // pass 内 shader 槽位很少（≤ ShaderStageCount），线性扫描即可。
        eastl::fixed_vector<const ShaderAsset*, RHI::ShaderStageCount> processed;

        // 跨 asset 的 stage 唯一性检查：每个 stage 最多被一个 asset 贡献反射。
        eastl::array<const ShaderAsset*, RHI::ShaderStageCount> stageOwner{};
        stageOwner.fill(nullptr);

        for (const ShaderAsset* shader : shaders)
        {
            if (shader == nullptr)
            {
                continue;
            }

            bool seen = false;
            for (const ShaderAsset* p : processed)
            {
                if (p == shader)
                {
                    seen = true;
                    break;
                }
            }
            if (seen)
            {
                continue;
            }
            processed.push_back(shader);

            MergeAssetReflection(*shader, state, result, stageOwner.data());
        }

        return result;
    }
}
