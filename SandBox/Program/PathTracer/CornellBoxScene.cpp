#include "CornellBoxScene.h"

#include <Log/ILogSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Bus/AssetResolveBus.h>
#include <Resource/Model/ModelAsset.h>

namespace Spark::SandBox
{
    CornellBoxScene::CornellBoxScene() = default;
    CornellBoxScene::~CornellBoxScene() = default;

    bool CornellBoxScene::Init()
    {
        auto* assets = Service<Resource::AssetManager>::Get();
        m_model = assets->LoadAsset<Resource::ModelAsset>(
            Resource::AssetId::Of<Resource::ModelAsset>("sandbox://Model/PathTracer/CornellBox.gltf"));
        if (!m_model || !m_model->IsReady())
        {
            LOG_ERROR("[CornellBoxScene] Failed to load the Cornell Box model.");
            return false;
        }

        // Synchronous load on the main thread: resolve immediately through the same
        // handler used by the engine's model import workflow.
        Resource::AssetResolveBus::Broadcast(
            &Resource::AssetResolveBus::Events::ResolveModelAssetToScene, m_model);
        return true;
    }
}
