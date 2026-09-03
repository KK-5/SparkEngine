#pragma once

#include "UI/Bus/AssetEditBus.h"
#include <ECS/Common.h>
#include <Reflection/RTTI.h>
#include <Resource/Bus/AssetBus.h>
#include <Resource/AssetTypes.h>
#include <EASTL/vector.h>

namespace Spark::Resource
{
    class ModelAsset;
}

namespace Editor
{
    class AssetHandler final : public AssetEditBus::Handler,
                               public Spark::Resource::AssetBus::MultiHandler
    {
    public:
        AssetHandler();
        ~AssetHandler() override;

        void OnModelAssetDragToScene(const Spark::Resource::ModelAsset& asset) override;
        void OnAssetDragToComponent(
            Spark::Entity              entity,
            Spark::TypeId              componentType,
            Spark::TypeId              fieldId,
            Spark::Resource::AssetId   assetId,
            Spark::Resource::AssetType assetType) override;
        void OnMaterialDragToComponent(
            Spark::Entity            entity,
            Spark::TypeId            componentType,
            Spark::TypeId            fieldId,
            Spark::Resource::AssetId assetId) override;

        void OnAssetReady(Spark::Resource::Asset& asset) override;
        void OnAssetError(Spark::Resource::Asset& asset) override;

    private:
        //! Which terminal a bind resolves to. Chosen at drop time and consumed once the
        //! asset turns Ready, so it has to be carried: the drop site is the only place
        //! that knows whether the target field holds an asset id or a material reference.
        enum class BindKind : uint8_t
        {
            AssetId,
            Material,
        };

        //! Identity of a pending "asset → component field" assignment. The component
        //! instance is deliberately NOT stored (it would dangle across the async gap);
        //! the resolver re-fetches the live component by reflection at resolve time.
        struct PendingComponentBind
        {
            Spark::Resource::AssetId   assetId;
            Spark::Entity              entity;
            Spark::TypeId              componentType;
            Spark::TypeId              fieldId;
            Spark::Resource::AssetType assetType;
            BindKind                   kind;
        };

        void QueueComponentBind(PendingComponentBind bind);

        void ResolvePendingScene(Spark::Resource::Asset& asset);
        void DropPendingScene(const Spark::Resource::AssetId& assetId);

        void ResolvePendingBinds(const Spark::Resource::AssetId& assetId);
        void DropPendingBinds(const Spark::Resource::AssetId& assetId);

        eastl::vector<Spark::Resource::AssetId> m_loadingAssets;
        eastl::vector<PendingComponentBind>     m_pendingBinds;
    };
}
