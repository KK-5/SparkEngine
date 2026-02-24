/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

namespace Spark::RHI
{
    //! Supported operations for rendering predication.
    enum class PredicationOp : uint32_t
    {
        EqualZero = 0,  ///< Enables predication if predication value is zero.
        NotEqualZero,   ///< Enables predication if predication value is not zero.
        Count
    };

    class CommandList
    {
    public:
        /// Assigns a list of viewports to the raster stage of the graphics pipe.
        virtual void SetViewports(const Viewport* viewports, uint32_t count) = 0;

        /// Assigns a list of scissors to the raster stage of the graphics pipe.
        virtual void SetScissors(const Scissor* scissors, uint32_t count) = 0;

        /// Assigns a scissor to the raster stage of the graphics pipe.
        void SetScissor(const Scissor& scissor)
        {
            SetScissors(&scissor, 1);
        }

        /// Assigns a viewport to the raster stage of the graphics pipe
        void SetViewport(const Viewport& viewport)
        {
            SetViewports(&viewport, 1);
        }

        //! Assigns a shader resource group for draw on the graphics pipe, at the binding slot
        //! determined by the layout used to create the shader resource group.
        //! @param shaderResourceGroup The shader resource group to bind.
        //virtual void SetShaderResourceGroupForDraw(const ShaderResourceGroup& shaderResourceGroup) = 0;

        //! Assigns a shader resource group for dispatch on compute pipe, at the binding slot
        //! determined by the layout used to create the shader resource group.
        //! @param shaderResourceGroup The shader resource group to bind.
        //virtual void SetShaderResourceGroupForDispatch(const ShaderResourceGroup& shaderResourceGroup) = 0;

    };
}