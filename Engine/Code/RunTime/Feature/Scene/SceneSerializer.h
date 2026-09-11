#pragma once

#include <EASTL/string_view.h>

#include <ECS/ContextStorage.h>
#include <ECS/Entity.h>
#include <Material/MaterialHandle.h>
#include <Serialization/Json.h>

namespace Spark::Scene
{
    //! The scene file's contents, from the two contexts a scene is made of. Takes storages
    //! rather than live contexts, so a staging context is written the same way.
    //!
    //! Returns false when a component failed to encode; `out` then holds everything else.
    bool WriteScene(const ContextStorage<Entity>& world,
                    const ContextStorage<Material::MaterialHandle>& materials,
                    JsonValue& out);

    //! The live scene, written to `virtualPath`. Nothing is written unless every component
    //! encoded: an incomplete file would read back as a scene that quietly lost something.
    bool SaveScene(eastl::string_view virtualPath);
}
