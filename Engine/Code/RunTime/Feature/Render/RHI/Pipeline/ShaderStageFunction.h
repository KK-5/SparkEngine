/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Object/Object.h>

#include "ShaderStages.h"

namespace Spark::RHI
{
    //! Contains byte code associated with a specific entry point function of a shader stage. This
    //! data is provided to the PipelineStateDescriptor when building a PSO. Certain platforms may
    //! utilize function constants to specialize the same central byte code store. Thus, a
    //! ShaderStageFunction instance is a child of a ShaderStageLibrary container.
    //!
    //! Each platform specializes this data structure with platform-specific data necessary to compile
    //! an entry point of a shader stage on a PSO. The platform-independent runtime does not need to care
    //! about specifics, the function is merely an opaque data stream passed to the pipeline state descriptor.
    class  ShaderStageFunction : public Object
    {
    public:
        virtual ~ShaderStageFunction() = default;

        //! Returns the shader stage associated with this function.
        ShaderStage GetShaderStage() const;

        //! Returns the hash computed for this function. Each platform implementation
        //! must calculate and store the hash from the platform-specific data.
        size_t GetHash() const;

        //! Finalizes and validates the function data. This must be called after manipulating the
        //! data manually, prior to serialization or use by the RHI runtime. It is *not* necessary
        //! to call this method on a serialized-in instance.
        ResultCode Finalize();
    
    protected:
        ShaderStageFunction() = default;
        ShaderStageFunction(ShaderStage shaderStage);

        //! The platform implementation must assign the hash value in the FinalizeInternal method,
        //! or the platform independent validation layer will fail with an error.
        void SetHash(size_t hash);

    private:

        ///////////////////////////////////////////////////////////////////
        // Platform API

        //! Finalizes the platform-dependent function data.
        virtual ResultCode FinalizeInternal() = 0;

        ///////////////////////////////////////////////////////////////////

        /// The shader stage associated with this descriptor.
        ShaderStage m_shaderStage = ShaderStage::Unknown;

        /// The computed hash of the shader byte-codes.
        size_t m_hash {0};
    };
}