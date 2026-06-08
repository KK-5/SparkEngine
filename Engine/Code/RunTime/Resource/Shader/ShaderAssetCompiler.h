#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <Resource/Asset.h>
#include "ShaderAsset.h"

struct IDxcUtils;
struct IDxcCompiler3;

namespace Spark::Resource
{
    // ShaderStageEntry moved to ShaderAsset.h — it is compile config and now
    // lives on ShaderDescriptor. This header sees it via "ShaderAsset.h".

    class ShaderAssetCompiler
    {
    public:
        ShaderAssetCompiler();
        ~ShaderAssetCompiler();

        //! Stateless w.r.t. compile config: backend + stages come entirely from
        //! the ShaderDescriptor. The compiler owns only the DXC tooling, so one
        //! instance can compile any shader (and is safe to reuse across shaders).
        eastl::unique_ptr<AssetData> Compile(const AssetId& id, AssetData& rawData,
            const eastl::vector<eastl::string>& searchPaths,
            const ShaderDescriptor& descriptor);

    private:
        bool InitDxc();

        IDxcUtils* m_utils{nullptr};
        IDxcCompiler3* m_compiler{nullptr};
    };
}
