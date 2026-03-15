/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Math/Interval.h>
#include <Object/ObjectName.h>
#include <RHI/Resource/Sampler/SamplerState.h>

namespace Spark::RHI
{
    //! A "ShaderInput" describes an input into a ShaderResourceGroup. Shader inputs are comprised of
    //! Buffers, Images, Samplers, and Constants. The former three shader inputs each contain an array
    //! of their respective resources. All of the resources in a shader input are identical with respect
    //! to their usage and type. Each of the {Buffer, Image, Sampler} inputs map directly to a variable
    //! definition in the shader source file.
    //!
    //! Constants are a bit different. Each constant input maps to a named constant variable in the
    //! shader resource group's implicit constant buffer. However, instead of a 'resource group' of
    //! constants, the constants occupy disjoint byte regions in an internal constant buffer.
    //!
    //! Each shader input has an id which is used to reflect the input by name.
    enum class ShaderInputType : uint32_t
    {
        Buffer = 0,
        Image,
        Sampler,
        Constant,
        Count
    };

    static const uint32_t ShaderInputTypeCount = static_cast<uint32_t>(ShaderInputType::Count);

    enum class ShaderInputBufferAccess : uint32_t
    {
        Constant = 0,
        Read,
        ReadWrite
    };

    enum class ShaderInputBufferType : uint32_t
    {
        Unknown = 0,
        Constant,
        Structured,
        Typed,
        Raw,
        AccelerationStructure
    };

    static const uint32_t UndefinedRegisterSlot = static_cast<uint32_t>(-1);

    class ShaderInputBufferDescriptor final
    {
    public:
        ShaderInputBufferDescriptor() = default;
        ShaderInputBufferDescriptor(
            const ObjectName& name,
            ShaderInputBufferAccess access,
            ShaderInputBufferType type,
            uint32_t bufferCount,
            uint32_t strideSize,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        //! The name id used to reflect the buffer input.
        ObjectName m_name;

        //! The type of the buffer for all array elements in the buffer input.
        ShaderInputBufferType m_type = ShaderInputBufferType::Unknown;

        //! How the array elements in the buffer input are accessed.
        ShaderInputBufferAccess m_access = ShaderInputBufferAccess::Read;

        //! Number of buffers array elements.
        uint32_t m_count = 0;

        //! Size of each buffer array element.
        uint32_t m_strideSize = 0;
            
        //! Register id of the resource in the SRG.
        //! This is only valid if the platform compiles the SRGs using "spaces".
        //! If not, this same information will be in the PipelineLayoutDescriptor.
        //! Some platforms (like Vulkan) need the register number when creating the
        //! SRG, others need it when creating the PipelineLayout.
        uint32_t m_registerId = UndefinedRegisterSlot;

        //! Logical Register Space that the register id is within.
        //! This is primarily used when an SRG contains one or more unbounded arrays,
        //! as an unbounded array contains all register ids in a register space.
        //! If an SRG doesn't contain any unbounded arrays all resources in it 
        //! will use the same space id.
        uint32_t m_spaceId = UndefinedRegisterSlot;
    };



    enum class ShaderInputImageAccess : uint32_t
    {
        Read = 0,
        ReadWrite
    };

    enum class ShaderInputImageType : uint32_t
    {
        Unknown = 0,
        Image1D,
        Image1DArray,
        Image2D,
        Image2DArray,
        Image2DMultisample,
        Image2DMultisampleArray,
        Image3D,
        ImageCube,
        ImageCubeArray,
        SubpassInput
    };

    class ShaderInputImageDescriptor final
    {
    public:

        ShaderInputImageDescriptor() = default;
        ShaderInputImageDescriptor(
            const ObjectName& name,
            ShaderInputImageAccess access,
            ShaderInputImageType type,
            uint32_t imageCount,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        //! The name id used to reflect the image input.
        ObjectName m_name;

        //! The type of image required for this shader input.
        ShaderInputImageType m_type = ShaderInputImageType::Unknown;

        //! How the array elements in the image input are accessed.
        ShaderInputImageAccess m_access = ShaderInputImageAccess::Read;

        //! Number of images array elements.
        uint32_t m_count = 0;
            
        //! Register id of the resource in the SRG.
        //! This is only valid if the platform compiles the SRGs using "spaces".
        //! If not, this same information will be in the PipelineLayoutDescriptor.
        //! Some platforms (like Vulkan) need the register number when creating the
        //! SRG, others need it when creating the PipelineLayout.
        uint32_t m_registerId = UndefinedRegisterSlot;

        //! Logical Register Space that the register id is within.
        //! This is primarily used when an SRG contains one or more unbounded arrays,
        //! as an unbounded array contains all register ids in a register space.
        //! If an SRG doesn't contain any unbounded arrays all resources in it 
        //! will use the same space id.
        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputBufferUnboundedArrayDescriptor final
    {
    public:
        ShaderInputBufferUnboundedArrayDescriptor() = default;
        ShaderInputBufferUnboundedArrayDescriptor(
            const ObjectName& name,
            ShaderInputBufferAccess access,
            ShaderInputBufferType type,
            uint32_t strideSize,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        //! The name id used to reflect the buffer input.
        ObjectName m_name;

        //! The type of buffer required for this shader input.
        ShaderInputBufferType m_type = ShaderInputBufferType::Unknown;

        //! How the array elements in the unbounded array input are accessed.
        ShaderInputBufferAccess m_access = ShaderInputBufferAccess::Read;

        //! Size of each buffer array element.
        uint32_t m_strideSize = 0;

        //! Register id of the resource in the SRG.
        //! This is only valid if the platform compiles the SRGs using "spaces".
        //! If not, this same information will be in the PipelineLayoutDescriptor.
        //! Some platforms (like Vulkan) need the register number when creating the
        //! SRG, others need it when creating the PipelineLayout.
        uint32_t m_registerId = UndefinedRegisterSlot;

        //! Logical Register Space that the register id is within.
        //! This is primarily used when an SRG contains one or more unbounded arrays,
        //! as an unbounded array contains all register ids in a register space.
        //! If an SRG doesn't contain any unbounded arrays all resources in it 
        //! will use the same space id.
        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputImageUnboundedArrayDescriptor final
    {
    public:
        ShaderInputImageUnboundedArrayDescriptor() = default;
        ShaderInputImageUnboundedArrayDescriptor(
            const ObjectName& name,
            ShaderInputImageAccess access,
            ShaderInputImageType type,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        //! The name id used to reflect the image input.
        ObjectName m_name;

        //! The type of image required for this shader input.
        ShaderInputImageType m_type = ShaderInputImageType::Unknown;

        //! How the array elements in the unbounded array input are accessed.
        ShaderInputImageAccess m_access = ShaderInputImageAccess::Read;

        //! Register id of the resource in the SRG.
        //! This is only valid if the platform compiles the SRGs using "spaces".
        //! If not, this same information will be in the PipelineLayoutDescriptor.
        //! Some platforms (like Vulkan) need the register number when creating the
        //! SRG, others need it when creating the PipelineLayout.
        uint32_t m_registerId = UndefinedRegisterSlot;

        //! Logical Register Space that the register id is within.
        //! This is primarily used when an SRG contains one or more unbounded arrays,
        //! as an unbounded array contains all register ids in a register space.
        //! If an SRG doesn't contain any unbounded arrays all resources in it 
        //! will use the same space id.
        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputSamplerDescriptor final
    {
    public:
        ShaderInputSamplerDescriptor() = default;
        ShaderInputSamplerDescriptor(const ObjectName& name, uint32_t samplerCount, uint32_t registerId, uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        //! The name id used to reflect the sampler input.
        ObjectName m_name;

        //! Number of sampler array elements.
        uint32_t m_count = 0;
            
        //! Register id of the resource in the SRG.
        //! This is only valid if the platform compiles the SRGs using "spaces".
        //! If not, this same information will be in the PipelineLayoutDescriptor.
        //! Some platforms (like Vulkan) need the register number when creating the
        //! SRG, others need it when creating the PipelineLayout.
        uint32_t m_registerId = UndefinedRegisterSlot;

        //! Logical Register Space that the register id is within.
        //! This is primarily used when an SRG contains one or more unbounded arrays,
        //! as an unbounded array contains all register ids in a register space.
        //! If an SRG doesn't contain any unbounded arrays all resources in it 
        //! will use the same space id.
        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputConstantDescriptor final
    {
    public:
        ShaderInputConstantDescriptor() = default;
        ShaderInputConstantDescriptor(
            const ObjectName& name,
            uint32_t constantByteOffset,
            uint32_t constantByteCount,
            uint32_t registerId,
            uint32_t spaceId);

        //! Returns the 64-bit hash of the binding.
        size_t GetHash(size_t seed = 0) const;

        //! The name id used to reflect the constant input.
        ObjectName m_name;

        //! The offset from the start of the constant buffer in bytes.
        uint32_t m_constantByteOffset = 0;

        //! The number of bytes 
        uint32_t m_constantByteCount = 0;
            
        //! Register id of the resource in the SRG.
        //! This is only valid if the platform compiles the SRGs using "spaces".
        //! If not, this same information will be in the PipelineLayoutDescriptor.
        //! Some platforms (like Vulkan) need the register number when creating the
        //! SRG, others need it when creating the PipelineLayout.
        uint32_t m_registerId = UndefinedRegisterSlot;

        //! Logical Register Space that the register id is within.
        //! This is primarily used when an SRG contains one or more unbounded arrays,
        //! as an unbounded array contains all register ids in a register space.
        //! If an SRG doesn't contain any unbounded arrays all resources in it 
        //! will use the same space id.
        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputStaticSamplerDescriptor final
    {
    public:
        ShaderInputStaticSamplerDescriptor() = default;
        ShaderInputStaticSamplerDescriptor(const ObjectName& name, const SamplerState& samplerState, uint32_t registerId, uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        //! The name id used to reflect the static sampler input.
        ObjectName m_name;
            
        //! The state of this static sampler.
        SamplerState m_samplerState;

        //! Register id of the resource in the SRG.
        //! This is only valid if the platform compiles the SRGs using "spaces".
        //! If not, this same information will be in the PipelineLayoutDescriptor.
        //! Some platforms (like Vulkan) need the register number when creating the
        //! SRG, others need it when creating the PipelineLayout.
        uint32_t m_registerId = UndefinedRegisterSlot;

        //! Logical Register Space that the register id is within.
        //! This is primarily used when an SRG contains one or more unbounded arrays,
        //! as an unbounded array contains all register ids in a register space.
        //! If an SRG doesn't contain any unbounded arrays all resources in it 
        //! will use the same space id.
        uint32_t m_spaceId = UndefinedRegisterSlot;
    };
}