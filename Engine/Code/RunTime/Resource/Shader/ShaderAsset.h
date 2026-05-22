#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>

#include <Resource/Asset.h>
#include <RHI/Pipeline/ShaderStages.h>

namespace Spark::Resource
{
    /// DXC 编译目标
    enum class ShaderBackend : uint32_t
    {
        DXIL,       ///< DX12 (sm 6.x)
        SPIRV,      ///< Vulkan
    };

    /// 单个 shader stage 的编译产物
    struct ShaderStageBytecode
    {
        RHI::ShaderStage       stage{RHI::ShaderStage::Unknown};
        eastl::string          entryPoint;         ///< 入口函数名，如 "VSMain", "PSMain"
        eastl::vector<uint8_t> bytecode;           ///< DXIL 或 SPIR-V 字节码
    };

    /// Shader 资产数据——包含同一份 HLSL 源码编译出的所有 stage 字节码
    class ShaderAssetData : public AssetData
    {
    public:
        ShaderAssetData() = default;

        void AddStageBytecode(ShaderStageBytecode bytecode);

        const ShaderStageBytecode* GetStageBytecode(RHI::ShaderStage stage) const;

        bool HasStage(RHI::ShaderStage stage) const;

        ShaderBackend GetBackend() const { return m_backend; }
        void SetBackend(ShaderBackend backend) { m_backend = backend; }

        eastl::string GetSourcePath() const { return m_resolvedPath; }
        void SetSourcePath(eastl::string_view path) { m_resolvedPath = path; }

    private:
        ShaderBackend m_backend{ShaderBackend::DXIL};
        eastl::unordered_map<RHI::ShaderStage, ShaderStageBytecode> m_stages;
        eastl::string m_resolvedPath;
    };


    class ShaderAsset : public Asset
    {
    public:
        static constexpr AssetType GetAssetTypeStatic() { return AssetType::Shader; }

        ShaderAsset(AssetId id);

        const ShaderAssetData* GetShaderData() const;
        const ShaderStageBytecode* GetStageBytecode(RHI::ShaderStage stage) const;
        bool HasStage(RHI::ShaderStage stage) const;
    };
}
