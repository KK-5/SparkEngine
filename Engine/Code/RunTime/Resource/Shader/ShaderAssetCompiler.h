#pragma once

#include <EASTL/vector.h>

#include <Resource/Asset.h>
#include "ShaderAsset.h"

struct IDxcUtils;
struct IDxcCompiler3;

namespace Spark::Asset
{
    /// Shader stage 编译描述
    struct ShaderStageEntry
    {
        RHI::ShaderStage stage;
        eastl::string entryPoint;       ///< 入口函数名，如 "VSMain"
        eastl::string targetProfile;    ///< 编译目标，如 "vs_6_0"
    };

    class ShaderAssetCompiler : public AssetCompiler
    {
    public:
        ShaderAssetCompiler(ShaderBackend backend = ShaderBackend::DXIL);
        ~ShaderAssetCompiler();

        /// 注册需要编译的 stage（可多次调用添加多个 stage）
        void AddStageEntry(ShaderStageEntry entry);

        eastl::unique_ptr<AssetData> Compile(const AssetId& id, AssetData& rawData) override;

    private:
        bool InitDxc();

        ShaderBackend m_backend;
        eastl::vector<ShaderStageEntry> m_stageEntries;

        IDxcUtils* m_utils{nullptr};
        IDxcCompiler3* m_compiler{nullptr};
    };
}
