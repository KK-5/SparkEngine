#pragma once

#include <Base.h>
#include <ECS/Common.h>

namespace Spark::Resource
{
    class ModelAsset;
}

namespace Spark::Mesh
{

    void ExtractMeshToWorld(Ptr<Resource::ModelAsset> model, WorldContext& context);

}