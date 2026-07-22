#pragma once

#include <Base.h>
#include <ECS/Common.h>

namespace Spark::Resource
{
    class ModelAsset;
}

namespace Spark::Spawn
{
    //! Instantiate a ready ModelAsset into the world: builds the node transform
    //! hierarchy, one entity per primitive with a MeshComponent, and — seeded from the
    //! model's embedded materials — a MaterialComponent per primitive (materials shared
    //! within this instantiation by materialIndex). This is the composition layer above
    //! Mesh + Material + Transform; neither of those features depends on the other.
    void SpawnModel(Ptr<Resource::ModelAsset> model, WorldContext& context);
}
