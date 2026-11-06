#pragma once

#include <Base.h>

namespace Spark::Resource
{
    class ModelAsset;
}

namespace Spark::SandBox
{
    // Loads the sample model and resolves it into the world through AssetResolveBus.
    // WorldContext owns the spawned entities until app teardown.
    class CornellBoxScene
    {
    public:
        CornellBoxScene();
        ~CornellBoxScene();

        bool Init();

    private:
        Ptr<Resource::ModelAsset> m_model;
    };
}
