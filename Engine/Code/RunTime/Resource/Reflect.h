#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Serialization/MetaFieldTraits.h>

#include <RHI/Pipeline/ShaderStages.h>

#include "Image/ImageAsset.h"
#include "Model/ModelAsset.h"
#include "Shader/ShaderAsset.h"

namespace Spark::Resource
{
    //! Descriptor reflection, which is what lets an AssetId's descriptor be written to a
    //! file and read back.
    //!
    //! Every enumerator name here is the on-disk format: renaming one silently invalidates
    //! every file that already spells it. Add values, never rename or repurpose them.
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<TextureCompression>()
            .Type("TextureCompression")
            .Data<TextureCompression::None>("None")
            .Data<TextureCompression::BC1_RGB>("BC1_RGB")
            .Data<TextureCompression::BC3_RGBA>("BC3_RGBA")
            .Data<TextureCompression::BC4_R>("BC4_R")
            .Data<TextureCompression::BC5_RG>("BC5_RG")
            .Data<TextureCompression::BC6H_HDR>("BC6H_HDR")
            .Data<TextureCompression::BC7_RGBA>("BC7_RGBA");

        context.Reflect<ImageColorSpace>()
            .Type("ImageColorSpace")
            .Data<ImageColorSpace::Linear>("Linear")
            .Data<ImageColorSpace::sRGB>("sRGB");

        context.Reflect<ImageUsage>()
            .Type("ImageUsage")
            .Data<ImageUsage::Texture2D>("Texture2D")
            .Data<ImageUsage::EnvironmentCubemap>("EnvironmentCubemap")
            .Data<ImageUsage::NoColorTexture2D>("NoColorTexture2D")
            .Data<ImageUsage::NormalMap>("NormalMap")
            .Data<ImageUsage::IrradianceCubemap>("IrradianceCubemap")
            .Data<ImageUsage::PrefilteredCubemap>("PrefilteredCubemap");

        context.Reflect<ModelAssetType>()
            .Type("ModelAssetType")
            .Data<ModelAssetType::Unknown>("Unknown")
            .Data<ModelAssetType::GLTF>("GLTF");

        context.Reflect<ShaderBackend>()
            .Type("ShaderBackend")
            .Data<ShaderBackend::DXIL>("DXIL")
            .Data<ShaderBackend::SPIRV>("SPIRV");

        // Count and GraphicsCount are bookkeeping, not stages -- and GraphicsCount shares
        // Compute's value, so reflecting it would let a Compute stage be written out under
        // the wrong name.
        context.Reflect<RHI::ShaderStage>()
            .Type("ShaderStage")
            .Data<RHI::ShaderStage::Unknown>("Unknown")
            .Data<RHI::ShaderStage::Vertex>("Vertex")
            .Data<RHI::ShaderStage::Geometry>("Geometry")
            .Data<RHI::ShaderStage::Fragment>("Fragment")
            .Data<RHI::ShaderStage::Compute>("Compute")
            .Data<RHI::ShaderStage::RayTracing>("RayTracing");

        context.Reflect<ImageAssetDescriptor>()
            .Type("ImageAssetDescriptor")
            .Data<&ImageAssetDescriptor::compression>("compression")
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ImageAssetDescriptor::colorSpace>("colorSpace")
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ImageAssetDescriptor::maxMipLevels>("maxMipLevels")
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ImageAssetDescriptor::usage>("usage")
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ImageAssetDescriptor::cubemapFaceSize>("cubemapFaceSize")
                .Traits(MetaFieldTraits::Serializable);

        context.Reflect<ModelAssetDescriptor>()
            .Type("ModelAssetDescriptor")
            .Data<&ModelAssetDescriptor::type>("type")
                .Traits(MetaFieldTraits::Serializable);

        context.Reflect<ShaderStageEntry>()
            .Type("ShaderStageEntry")
            .Data<&ShaderStageEntry::stage>("stage")
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ShaderStageEntry::entryPoint>("entryPoint")
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ShaderStageEntry::targetProfile>("targetProfile")
                .Traits(MetaFieldTraits::Serializable);

        context.Reflect<ShaderDescriptor>()
            .Type("ShaderDescriptor")
            .Data<&ShaderDescriptor::backend>("backend")
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ShaderDescriptor::stages>("stages")
                .Traits(MetaFieldTraits::Serializable);
    }
}
