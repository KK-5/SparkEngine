#pragma once

#include <EASTL/span.h>

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/ShaderStages.h>
#include <Resource/Shader/ShaderAsset.h>

namespace Spark::Resource
{
    /// 新路径（ShaderInput）反射结果：单次 AddShaderInputDescriptors 调用所需的全部数据。
    /// stageMask 是 shader 中所有有反射数据的 stage 的并集。
    struct ShaderInputBuildResult
    {
        RHI::ShaderInputList list;
        RHI::ShaderStageMask stageMask = RHI::ShaderStageMask::None;
    };

    /// 从 ShaderAsset 反射构建 ShaderInputList。跨 stage 去重：
    ///   - Buffer/Image/Sampler 按 (spaceId, registerId, type) 去重；
    ///   - Constant 按变量名去重（同 cbuffer 的每个变量展开为独立 descriptor，
    ///     Finalize 时由 PipelineLayoutDescriptor 按 registerId 聚合为 ConstantBufferLayout）。
    /// 调用方负责将结果传给 PipelineLayoutDescriptor::AddShaderInputDescriptors。
    ///
    /// 反射→RHI descriptor 的转换是层级中性的（只依赖 RHI 与 Resource::ShaderAsset），
    /// 因此放在资产层，供渲染层与资产层（如 EnvironmentBaker）共用。
    ShaderInputBuildResult BuildShaderInputList(const ShaderAsset& shader);

    /// 多 ShaderAsset 版本：把 VS / FS / GS / CS 等多个 shader 的反射一并合并去重。
    /// 用于一个 RenderPass 含多个 stage 来自不同 HLSL 文件的场景。
    ///   - nullptr 元素被忽略；
    ///   - 同一个指针重复出现（同一组合 HLSL 同时挂到多个 stage 槽位）只处理一次；
    ///   - 同一个 ShaderStage 被两个不同 ShaderAsset 都反射出来 → ASSERT 失败
    ///     （一个 pass 不能为同一 stage 提供两个 shader）。
    ShaderInputBuildResult BuildShaderInputList(
        eastl::span<const ShaderAsset* const> shaders);
}
