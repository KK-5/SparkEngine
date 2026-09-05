#pragma once

#include <cstdint>

#include <EASTL/string.h>

#include <Reflection/TypeRegistry.h>
#include <Resource/AssetTypes.h>
#include <Resource/Material/MaterialState.h>
#include <Resource/Material/StandardPBR.h>

namespace Editor
{
    //! A copy of one of a material entity's components, through the reflected ops -- the
    //! editor addresses ECS state by (type, entity) and never holds a context.
    template <typename T>
    bool ReadMaterialComponent(uint32_t handleId, T& out)
    {
        Spark::MetaType type = Spark::TypeRegistry::GetContext().Resolve<T>();
        if (!type)
        {
            return false;
        }
        auto getFn = type.func("GetComponent"_hs);
        if (!getFn)
        {
            return false;
        }

        Spark::MetaAny ptr = getFn.invoke({}, handleId);
        if (!ptr || !(*ptr))
        {
            return false;
        }

        Spark::MetaAny instance = *ptr;
        if (const T* value = instance.try_cast<T>())
        {
            out = *value;
            return true;
        }
        return false;
    }

    //! The reverse of ReadMaterialComponent, through the same reflected ops.
    template <typename T>
    bool WriteMaterialComponent(uint32_t handleId, const T& value)
    {
        Spark::MetaType type = Spark::TypeRegistry::GetContext().Resolve<T>();
        if (!type)
        {
            return false;
        }
        auto setFn = type.func("AddOrReplaceComponent"_hs);
        if (!setFn)
        {
            return false;
        }

        Spark::MetaAny instance = type.construct();
        if (!instance)
        {
            return false;
        }
        instance.cast<T&>() = value;

        return static_cast<bool>(setFn.invoke({}, handleId, instance));
    }

    //! The two components a `.smat` is made of. Hands them back rather than the
    //! MaterialAssetData they go into: AssetData is neither copyable nor movable, so the
    //! caller builds one on the spot.
    bool ReadMaterialValues(uint32_t handleId, Spark::Resource::StandardPBR& params,
                            Spark::Resource::MaterialState& state);

    //! Whether a material entity is still there -- i.e. still carries StandardPBR. A handle
    //! is ABA-safe, but material entities CAN be destroyed (the ones an override is composed
    //! into are), so anything holding one across frames has to ask rather than assume.
    //!
    //! Takes the handle as a raw uint32 like the reflected component ops do, and answers
    //! through them, so the editor keeps addressing ECS state by (type, entity) alone.
    bool MaterialExists(uint32_t handleId);

    //! The asset backing a material entity, if one does. False for the resident default
    //! material and for anything else no asset created -- those carry no MaterialAssetRef.
    bool TryGetMaterialAsset(uint32_t handleId, Spark::Resource::AssetId& out);

    //! A material entity's asset identity as text. Three cases, all of them something the
    //! user needs told apart: the asset it came from, "(scene material)" for one no asset
    //! backs (the resident default today, scene-owned materials later), and "(none)" for a
    //! reference that resolves to nothing -- a deleted `.smat` lands there, and the object
    //! is quietly rendering with the default material.
    eastl::string MaterialIdentity(uint32_t handleId, bool exists);
}
