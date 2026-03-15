/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

 /*
 * Modified by SparkEngine in 2025
 *  -- Add AddRef/Release function so that Ptr can hold DescriptorHandle/DescriptorTable
 */

#pragma once

#include <DX12.h>

namespace Spark::RHI::DX12
{
    // DescriptorHandle和DescriptorTable有AddRef和Release函数，所以可以将其指针放入Ptr中，但Ptr不会对其生命周期进行管理，
    // 因为这两个对象并不持有任何资源，资源由DescriptorPool进行管理

    struct DescriptorHandle
    {
        static const uint32_t NullIndex = (uint32_t)-1;

        DescriptorHandle();
        DescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags, uint32_t index);

        bool IsNull() const;
        bool IsShaderVisible() const;

        void AddRef() {}
        void Release() {}

        DescriptorHandle operator+ (uint32_t offset) const;
        DescriptorHandle& operator+= (uint32_t offset);

        uint32_t m_index : 32;
        D3D12_DESCRIPTOR_HEAP_TYPE m_type : 4;
        D3D12_DESCRIPTOR_HEAP_FLAGS m_flags : 4;
    };

    class DescriptorTable
    {
    public:
        DescriptorTable() = default;
        DescriptorTable(DescriptorHandle handle, uint32_t count);

        DescriptorHandle operator [] (uint32_t i) const;

        DescriptorHandle GetOffset() const;
        D3D12_DESCRIPTOR_HEAP_TYPE GetType() const;
        D3D12_DESCRIPTOR_HEAP_FLAGS GetFlags() const;
        uint32_t GetSize() const;

        bool IsNull() const;
        bool IsValid() const;

        void AddRef() {}
        void Release() {}

    private:
        DescriptorHandle m_offset;
        uint32_t m_size = 0;
    };
}
