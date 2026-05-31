#pragma once

#include <Object/ObjectName.h>
#include <RHI/Pipeline/ShaderStages.h>
#include <RHI/Resource/Sampler/SamplerState.h>

namespace Spark::RHI
{

    using InputName = ObjectName;

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
            const InputName& name,
            ShaderInputBufferAccess access,
            ShaderInputBufferType type,
            uint32_t bufferCount,
            uint32_t strideSize,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        InputName m_name;

        ShaderInputBufferType m_type = ShaderInputBufferType::Unknown;

        ShaderInputBufferAccess m_access = ShaderInputBufferAccess::Read;

        uint32_t m_count = 0;

        uint32_t m_strideSize = 0;

        uint32_t m_registerId = UndefinedRegisterSlot;

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
            const InputName& name,
            ShaderInputImageAccess access,
            ShaderInputImageType type,
            uint32_t imageCount,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        InputName m_name;

        ShaderInputImageType m_type = ShaderInputImageType::Unknown;

        ShaderInputImageAccess m_access = ShaderInputImageAccess::Read;

        uint32_t m_count = 0;

        uint32_t m_registerId = UndefinedRegisterSlot;

        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputSamplerDescriptor final
    {
    public:
        ShaderInputSamplerDescriptor() = default;
        ShaderInputSamplerDescriptor(
            const InputName& name,
            uint32_t samplerCount,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        InputName m_name;

        uint32_t m_count = 0;

        uint32_t m_registerId = UndefinedRegisterSlot;

        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputConstantDescriptor final
    {
    public:
        ShaderInputConstantDescriptor() = default;

        //! Construct with stride-aware fields. ShaderInputConstant::SetData uses
        //! (elementCount, elementByteSize, elementStride) to split a caller's
        //! C++-packed upload into the HLSL CB layout — see ShaderAsset.h for the
        //! field semantics and how to derive them from reflection.
        ShaderInputConstantDescriptor(
            const InputName& name,
            uint32_t constantByteOffset,
            uint32_t constantByteCount,
            uint32_t elementCount,
            uint32_t elementByteSize,
            uint32_t elementStride,
            uint32_t registerId,
            uint32_t spaceId);

        //! Legacy constructor — assumes a single tightly packed element
        //! (elementCount=1, elementByteSize=elementStride=constantByteCount).
        //! Use for scalars/single vectors/single matrices whose C++ layout
        //! already matches HLSL CB layout (e.g., float4, float4x4). For arrays
        //! or odd-sized matrices (float3[N], float3x3, ...) the stride-aware
        //! constructor is required.
        ShaderInputConstantDescriptor(
            const InputName& name,
            uint32_t constantByteOffset,
            uint32_t constantByteCount,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        InputName m_name;

        uint32_t m_constantByteOffset = 0;

        //! HLSL CB 中变量占用的字节跨度（DXC 反射上报的 Size）。
        //! 注意：对 floatN[M] 这种数组，等于 `(M-1) * elementStride + elementByteSize`，
        //! trailing pad 不计入。
        uint32_t m_constantByteCount = 0;

        //! 见 ShaderConstantVariableReflection 字段注释。
        uint32_t m_elementCount    = 1;
        uint32_t m_elementByteSize = 0;
        uint32_t m_elementStride   = 0;

        uint32_t m_registerId = UndefinedRegisterSlot;

        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

    class ShaderInputStaticSamplerDescriptor final
    {
    public:
        ShaderInputStaticSamplerDescriptor() = default;
        ShaderInputStaticSamplerDescriptor(
            const InputName& name,
            const SamplerState& samplerState,
            uint32_t registerId,
            uint32_t spaceId);

        size_t GetHash(size_t seed = 0) const;

        InputName m_name;

        SamplerState m_samplerState;

        uint32_t m_registerId = UndefinedRegisterSlot;

        uint32_t m_spaceId = UndefinedRegisterSlot;
    };

} // namespace Spark::RHI
