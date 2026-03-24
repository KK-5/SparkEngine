#include "ShaderAsset.h"

namespace Spark::Asset
{
    // ---- ShaderAssetData ----

    void ShaderAssetData::AddStageBytecode(ShaderStageBytecode bytecode)
    {
        auto stage = bytecode.stage;
        m_stages[stage] = eastl::move(bytecode);
    }

    const ShaderStageBytecode* ShaderAssetData::GetStageBytecode(RHI::ShaderStage stage) const
    {
        auto it = m_stages.find(stage);
        return it != m_stages.end() ? &it->second : nullptr;
    }

    bool ShaderAssetData::HasStage(RHI::ShaderStage stage) const
    {
        return m_stages.find(stage) != m_stages.end();
    }

    // ---- ShaderAsset ----

    ShaderAsset::ShaderAsset(AssetId id)
        : Asset(eastl::move(id), AssetType::Shader)
    {}

    const ShaderAssetData* ShaderAsset::GetShaderData() const
    {
        return GetData<ShaderAssetData>();
    }

    const ShaderStageBytecode* ShaderAsset::GetStageBytecode(RHI::ShaderStage stage) const
    {
        auto* data = GetShaderData();
        return data ? data->GetStageBytecode(stage) : nullptr;
    }

    bool ShaderAsset::HasStage(RHI::ShaderStage stage) const
    {
        auto* data = GetShaderData();
        return data ? data->HasStage(stage) : false;
    }
}
