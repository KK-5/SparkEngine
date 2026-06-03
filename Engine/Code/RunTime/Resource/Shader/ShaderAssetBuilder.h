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
    /// 默认注册 VS/PS 两个 stage（VSMain / PSMain），与原 AssetManager 行为一致。
    class ShaderAssetBuilder final : public ISystem,
                                     public AssetBuildBus::Handler
    {
    public:
        ShaderAssetBuilder() : m_compiler(ShaderBackend::DXIL) {}
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
