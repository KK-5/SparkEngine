#pragma once

#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <Resource/Shader/ShaderAsset.h>

namespace Spark::Render
{
    /// 将 ShaderAsset 的反射数据填入 ShaderResourceLayout。不调用 Finalize()，由调用方负责。
    void BuildShaderResourceLayout(const Resource::ShaderAsset& shader, RHI::ShaderResourceLayout& layout);

    /// 从 ShaderAsset 的反射数据构建 ShaderResourceLayout（通过 RHI Factory 创建并 Finalize）。
    /// 遍历所有 stage，合并 cbuffer（含 CBV + 常量变量）和资源绑定，跨 stage 去重。
    Ptr<RHI::ShaderResourceLayout> BuildShaderResourceLayout(const Resource::ShaderAsset& shader);
}
