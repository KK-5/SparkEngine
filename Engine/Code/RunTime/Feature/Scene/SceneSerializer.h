#pragma once

#include <EASTL/string_view.h>

#include <ECS/ContextStorage.h>
#include <ECS/WorldContext.h>
#include <ECS/Entity.h>
#include <Material/MaterialContext.h>
#include <Material/MaterialHandle.h>
#include <Serialization/Json.h>

namespace Spark::Scene
{
    //! The scene file's contents, from the two contexts a scene is made of. A world entity
    //! is in the scene when it is in the scene graph, a material when it has an asset identity;
    //! what a system builds for itself (the editor camera, icons, the default material) has
    //! neither. Takes storages rather than live contexts, so a staging context is written the
    //! same way.
    //!
    //! Returns false when a component failed to encode; `out` then holds everything else.
    bool WriteScene(const ContextStorage<Entity>& world,
                    const ContextStorage<Material::MaterialHandle>& materials,
                    JsonValue& out);

    //! The live scene, written to `virtualPath`. Nothing is written unless every component
    //! encoded: an incomplete file would read back as a scene that quietly lost something.
    bool SaveScene(eastl::string_view virtualPath);

    //! The two contexts a scene file describes, merged into the live ones. Identifiers are kept
    //! where they are free and remapped where they are not, references following either way.
    //!
    //! Nothing is merged unless the whole file decoded: a scene half in the world is worse than
    //! a scene that refused to load.
    bool ReadScene(const JsonValue& in, WorldContext& world, Material::MaterialContext& materials);

    //! The scene at `virtualPath`, into the live contexts.
    bool LoadScene(eastl::string_view virtualPath);

    //! Take the scene out of the live contexts: what belongs to it by the same rule the writer
    //! uses. World entities are marked for the reaper; material entities have no reaper and go
    //! at once. What a system owns -- the editor camera, icons, the default material -- stays.
    void ClearScene(WorldContext& world, Material::MaterialContext& materials);
}
