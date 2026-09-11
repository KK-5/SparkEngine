#pragma once

#include <Base.h>
#include <ECS/Common.h>
#include <ECS/StagingContext.h>

namespace Spark::Resource
{
    class ModelAsset;
}

namespace Spark::Spawn
{
    //! Parse a ready ModelAsset into a staging context: one entity per node carrying its name and
    //! transform, one child entity per primitive carrying mesh and material, and a Hierarchy tree
    //! linking them. Nothing observes a staging context, so no system hears any of it.
    //!
    //! Split out from SpawnModel so the result can later be cached per ModelAsset and instantiated
    //! more than once through the Copy overload of Merge.
    StagingContext<Entity> BuildModelStaging(Ptr<Resource::ModelAsset> model);

    //! Instantiate a ready ModelAsset into the world: builds the node transform
    //! hierarchy, one entity per primitive with a MeshComponent, and — seeded from the
    //! model's embedded materials — a MaterialComponent per primitive (materials shared
    //! within this instantiation by materialIndex). This is the composition layer above
    //! Mesh + Material + Transform; neither of those features depends on the other.
    void SpawnModel(Ptr<Resource::ModelAsset> model, WorldContext& context);
}
