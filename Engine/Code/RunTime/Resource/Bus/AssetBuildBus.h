#pragma once

#include <EBus/EBus.h>

#include <Base.h>

#include <Resource/AssetTypes.h>


namespace Spark::Resource
{
    class Asset;
    class AssetBuildContext;

    /// 按 AssetType 寻址的"构建器"总线：每种 AssetType 注册唯一一个 handler，
    /// 实现 CreateAsset / Load / Compile 三个事件。
    ///
    /// - Load: 读 ctx.id 与 ctx.searchPaths，将源数据反序列化进 ctx.rawData
    /// - Compile: 读 ctx.rawData，将运行时数据写入 ctx.compiledData
    ///   （Compile 内可同步派发其它 type 的 Compile/Load 事件以处理子资产）
    /// - CreateAsset: 构造对应 AssetType 的 Asset 子类对象
    struct AssetBuildEvents : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Single;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = AssetType;

        virtual Ptr<Asset> CreateAsset(const AssetId& id) = 0;
        virtual void       Load(AssetBuildContext& ctx) = 0;
        virtual void       Compile(AssetBuildContext& ctx) = 0;
    };

    using AssetBuildBus = EBus<AssetBuildEvents>;
}
