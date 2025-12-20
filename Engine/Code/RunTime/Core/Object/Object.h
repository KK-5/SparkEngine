/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/atomic.h>

#include "ObjectName.h"

namespace Spark
{
    class Object
    {
    public:
        virtual ~Object() = default;

        void SetName(const ObjectName& name);

        const ObjectName& GetName() const;

        uint32_t UseCount() const
        {
            return static_cast<uint32_t>(m_useCount);
        }

        void AddRef();

        void Release();

        virtual void Init() {}
    
    protected:
        Object() = default;

        mutable eastl::atomic<uint32_t> m_useCount {0};
    private:
        /// @brief 通常情况下Shutdown在m_useCount减少到0时自动调用，不需要显式调用，如果子类有显式调用的需求可以重写它到public
        /// 默认情况下Shutdown不做任何资源清理操作，包括析构当前对象
        virtual void Shutdown() {}
        
        ObjectName m_name;
    };
}