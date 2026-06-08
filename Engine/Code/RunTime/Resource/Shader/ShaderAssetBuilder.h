#pragma once

#include <ECS/ISystem.h>
#include <Resource/Bus/AssetBuildBus.h>
#include <Resource/Common/CommonAssetLoader.h>

#include "ShaderAssetCompiler.h"


namespace Spark::Resource
{
    /// Shader 资产的构建器：长生命周期 System + AssetBuildBus Handler。
    /// Load 复用通用的 BinaryAssetLoader 直接读源文件字节；
    /// Compile 委托给 ShaderAssetCompiler 做 DXC 编译。
    /// 编译的 stage 由每个 shader 自己决定（按源码探测入口点，存进
    /// ShaderDescriptor），不再全局写死 VS/PS。
    class ShaderAssetBuilder final : public ISystem,
                                     public AssetBuildBus::Handler
    {
    public:
        ShaderAssetBuilder() = default;
        ~ShaderAssetBuilder() override = default;

        // ISystem
        eastl::vector<HashString> Request() const override { return {}; }
        HashString                GetName() const override;

        // AssetBuildBus::Handler
        Ptr<Asset> CreateAsset(const AssetId& id) override;
        void       Load(AssetBuildContext& ctx) override;
        void       Compile(AssetBuildContext& ctx) override;

    private:
        void InitInternal() override;
        void ShutdownInternal() override;

        BinaryAssetLoader   m_loader;
        ShaderAssetCompiler m_compiler;
    };
}
