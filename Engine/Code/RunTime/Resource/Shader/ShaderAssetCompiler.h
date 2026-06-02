#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <Resource/Asset.h>
#include "ShaderAsset.h"

struct IDxcUtils;
struct IDxcCompiler3;

namespace Spark::Resource
{
    /// Shader stage 编译描述
    struct ShaderStageEntry
    {
        RHI::ShaderStage stage;
        eastl::string entryPoint;       ///< 入口函数名，如 "VSMain"
        eastl::string targetProfile;    ///< 编译目标，如 "vs_6_0"
    };

    class ShaderAssetCompiler
    {
    public:
        ShaderAssetCompiler(ShaderBackend backend = ShaderBackend::DXIL);
        ~ShaderAssetCompiler();
        
        void AddStageEntry(ShaderStageEntry entry);

        eastl::unique_ptr<AssetData> Compile(const AssetId& id, AssetData& rawData,
            const eastl::vector<eastl::string>& searchPaths);

    private:
        bool InitDxc();

        ShaderBackend m_backend;
        eastl::vector<ShaderStageEntry> m_stageEntries;

        IDxcUtils* m_utils{nullptr};
        IDxcCompiler3* m_compiler{nullptr};
    };
}
