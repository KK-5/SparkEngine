#pragma once

#include <Base.h>

#include <Resource/Asset.h>
#include <Resource/AssetTypes.h>

#include "StandardPBR.h"

namespace Spark::Resource
{
    //! Deliberately empty. A descriptor is the REFERRING side's compile choice and folds
    //! into AssetId's identity hash -- usage picks how an image is compiled, backend picks
    //! which bytecode a shader becomes. A material has no such choice: what it is made of
    //! is authored inside the `.smat` and read from it. In particular `shadingModel` does
    //! NOT belong here -- putting it in identity would mean naming a material's shading
    //! model before opening the file, and would let two references disagree about one
    //! file's content. An empty descriptor is what makes one `.smat` exactly one identity.
    class MaterialAssetDescriptor : public AssetDescriptor
    {
    public:
        AssetHash Hash() const override;
    };

    //! The compiled material: the authored parameter set, ready to be copied onto a
    //! material entity. Only StandardPBR today -- a second shading model would carry a
    //! different parameter component, which this concrete member cannot express and which
    //! is a change to make when that model arrives, not before.
    class MaterialAssetData : public AssetData
    {
    public:
        const StandardPBR& GetParams() const { return m_params; }

    private:
        friend class MaterialAssetCompiler;
        friend class MaterialAssetBuilder;

        StandardPBR m_params;
    };

    class MaterialAsset : public Asset
    {
    public:
        using Descriptor = MaterialAssetDescriptor;

        static constexpr AssetType GetAssetTypeStatic() { return AssetType::Material; }
        static Ptr<AssetDescriptor> DefaultDescriptor();

        explicit MaterialAsset(AssetId id);

        const MaterialAssetData* GetMaterialData() const;
    };
}
