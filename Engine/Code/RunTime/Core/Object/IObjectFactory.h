/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

namespace Spark
{
    template <typename ObjectType>
    class IObjectFactory
    {
    public:
        virtual ~IObjectFactory() = default;

        struct Descriptor {};

        void Init(const Descriptor& descriptor) {}

        void Shutdown() {}

        /// Called when an object is being first created.
        template <typename... Args>
        ObjectType* Allocate(Args&&...)
        {
            return nullptr;
        }

        /// Called when a object collected object is being reset for new use.
        template <typename... Args>
        void ReAllocate(ObjectType* object, Args&&...)
        {
            (void)object;
        }

        /// Called when the object is being shutdown.
        void DeAllocate(ObjectType* object, bool isPoolShutdown)
        {
            (void)object;
            (void)isPoolShutdown;
        }

        /// Called when object collection has begun.
        void BeginCollect() {}

        /// Called when object collection has ended.
        void EndCollect() {}

        /// Called when the object is being object collected. Return true if the object should be recycled,
        /// or false if the object should be shutdown and released from the pool.
        bool RecycleObject(ObjectType* object)
        {
            (void)object;
            return true;
        }
    };
}