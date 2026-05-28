#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>

#include <Resource/Asset.h>
#include <RHI/Pipeline/ShaderStages.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>

namespace Spark::Resource
{
    /// DXC 编译目标
    enum class ShaderBackend : uint32_t
    {
        DXIL,       ///< DX12 (sm 6.x)
        SPIRV,      ///< Vulkan
    };

    // ---- Platform-agnostic shader reflection types ----

    /// cbuffer 内单个变量的反射信息
    struct ShaderConstantVariableReflection
    {
        eastl::string m_name;
        uint32_t m_byteOffset = 0;
        uint32_t m_byteSize = 0;
    };

    /// cbuffer 反射信息（含变量布局）
    struct ShaderConstantBufferReflection
    {
        eastl::string m_name;
        uint32_t m_byteSize = 0;
        uint32_t m_registerId = 0;
        uint32_t m_spaceId = 0;
        eastl::vector<ShaderConstantVariableReflection> m_variables;
    };

    /// 单个绑定资源（SRV / UAV / Sampler，不含 cbuffer）
    struct ShaderResourceBindingReflection
    {
        eastl::string                m_name;
        RHI::ShaderInputType         m_type         = RHI::ShaderInputType::Buffer;
        uint32_t                     m_registerId   = 0;
        uint32_t                     m_count        = 0;
        uint32_t                     m_spaceId      = 0;
        RHI::ShaderInputBufferAccess m_bufferAccess = RHI::ShaderInputBufferAccess::Read;
        RHI::ShaderInputBufferType   m_bufferType   = RHI::ShaderInputBufferType::Unknown;
        RHI::ShaderInputImageAccess  m_imageAccess  = RHI::ShaderInputImageAccess::Read;
        RHI::ShaderInputImageType    m_imageType    = RHI::ShaderInputImageType::Unknown;
    };

    /// 单个 stage 的反射数据
    struct ShaderStageReflection
    {
        eastl::vector<ShaderResourceBindingReflection> m_resources;
        eastl::vector<ShaderConstantBufferReflection>  m_cbuffers;

        uint32_t m_threadGroupSizeX = 1;
        uint32_t m_threadGroupSizeY = 1;
        uint32_t m_threadGroupSizeZ = 1;
    };

    /// Per-instance shader compile config. Lives on the AssetId so the same
    /// HLSL file compiled for DXIL vs SPIRV is two distinct assets.
    class ShaderDescriptor : public AssetDescriptor
    {
    public:
        ShaderBackend backend = ShaderBackend::DXIL;

        AssetHash Hash() const override;
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

        void AddStageReflection(RHI::ShaderStage stage, ShaderStageReflection reflection);

        const ShaderStageReflection* GetStageReflection(RHI::ShaderStage stage) const;

        bool HasStageReflection(RHI::ShaderStage stage) const;

        ShaderBackend GetBackend() const { return m_backend; }
        void SetBackend(ShaderBackend backend) { m_backend = backend; }

        eastl::string GetSourcePath() const { return m_resolvedPath; }
        void SetSourcePath(eastl::string_view path) { m_resolvedPath = path; }

    private:
        ShaderBackend m_backend{ShaderBackend::DXIL};
        eastl::unordered_map<RHI::ShaderStage, ShaderStageBytecode> m_stages;
        eastl::unordered_map<RHI::ShaderStage, ShaderStageReflection> m_reflections;
        eastl::string m_resolvedPath;
    };


    class ShaderAsset : public Asset
    {
    public:
        using Descriptor = ShaderDescriptor;

        static constexpr AssetType GetAssetTypeStatic() { return AssetType::Shader; }
        static Ptr<AssetDescriptor> DefaultDescriptor();

        ShaderAsset(AssetId id);

        const ShaderAssetData* GetShaderData() const;
        const ShaderStageBytecode* GetStageBytecode(RHI::ShaderStage stage) const;
        bool HasStage(RHI::ShaderStage stage) const;
        const ShaderStageReflection* GetStageReflection(RHI::ShaderStage stage) const;
    };
}
