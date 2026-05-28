#include "ShaderAsset.h"

#include <EASTLEX/hash.h>

namespace Spark::Resource
{
    // ---- ShaderDescriptor ----

    AssetHash ShaderDescriptor::Hash() const
    {
        size_t h = 0;
        eastl::hash_combine(h, static_cast<size_t>(backend));
        return static_cast<AssetHash>(h);
    }

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

    void ShaderAssetData::AddStageReflection(RHI::ShaderStage stage, ShaderStageReflection reflection)
    {
        m_reflections[stage] = eastl::move(reflection);
    }

    const ShaderStageReflection* ShaderAssetData::GetStageReflection(RHI::ShaderStage stage) const
    {
        auto it = m_reflections.find(stage);
        return it != m_reflections.end() ? &it->second : nullptr;
    }

    bool ShaderAssetData::HasStageReflection(RHI::ShaderStage stage) const
    {
        return m_reflections.find(stage) != m_reflections.end();
    }

    // ---- ShaderAsset ----

    Ptr<AssetDescriptor> ShaderAsset::DefaultDescriptor()
    {
        static Ptr<AssetDescriptor> instance(new ShaderDescriptor{});
        return instance;
    }

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

    const ShaderStageReflection* ShaderAsset::GetStageReflection(RHI::ShaderStage stage) const
    {
        auto* data = GetShaderData();
        return data ? data->GetStageReflection(stage) : nullptr;
    }
}
