#pragma once

#include <RHI/Device/DeviceObject.h>
#include <RHI/Resource/Buffer/Buffer.h>

namespace Spark::RHI
{
    enum class AccelerationStructureType : uint8_t
    {
        BottomLevel = 0,
        TopLevel
    };

    struct AccelerationStructureDescriptor
    {
        AccelerationStructureType m_type   = AccelerationStructureType::BottomLevel;
        //! Bound with only RayTracingAccelerationStructure.
        const Buffer*             m_buffer = nullptr;
        //! A multiple of Alignment::AccelerationStructure.
        uint64_t                  m_offset = 0;
        uint64_t                  m_size   = 0;
    };

    //! A range of a buffer read as an acceleration structure. Holds no memory: memory, state
    //! and barriers are the buffer's. The RHI does not track which structures a buffer holds.
    class AccelerationStructure : public DeviceObject
    {
    public:
        virtual ~AccelerationStructure() = default;

        ResultCode Init(Device& device, const AccelerationStructureDescriptor& descriptor);

        AccelerationStructureType GetType() const;
        const Buffer&             GetBuffer() const;
        uint64_t                  GetOffset() const;
        uint64_t                  GetSize() const;

        //! What an instance refers to its bottom level structure by.
        virtual uint64_t GetDeviceAddress() const = 0;

    protected:
        AccelerationStructure() = default;

        void Shutdown() override final;

    private:
        bool ValidateForInit(const AccelerationStructureDescriptor& descriptor) const;

        virtual ResultCode InitInternal(Device& device, const AccelerationStructureDescriptor& descriptor) = 0;
        virtual void       ShutdownInternal() = 0;

        AccelerationStructureType m_type = AccelerationStructureType::BottomLevel;
        ConstPtr<Buffer>          m_buffer;
        uint64_t                  m_offset = 0;
        uint64_t                  m_size   = 0;
    };
}
