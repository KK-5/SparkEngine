/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <RHI/SwapChain/SwapChain.h>
#include <DX12.h>

namespace Spark::RHI::DX12
{
    class Device;

    class SwapChain final : public RHI::SwapChain
    {
    public:
        Device& GetDevice() const;
    
    private:
        SwapChain() = default;

        //////////////////////////////////////////////////////////////////////////
        // RHI::SwapChain
        RHI::ResultCode InitInternal(RHI::Device& deviceBase, const RHI::SwapChainDescriptor& descriptor, RHI::SwapChainDimensions* nativeDimensions) override;
        // void ShutdownInternal() override;
        uint32_t PresentInternal() override;
        RHI::ResultCode InitImageInternal(const InitImageRequest& request) override;
        // void ShutdownResourceInternal(RHI::DeviceResource& resourceBase) override;
        RHI::ResultCode ResizeInternal(const RHI::SwapChainDimensions& dimensions, RHI::SwapChainDimensions* nativeDimensions) override;
        bool IsExclusiveFullScreenPreferred() const override;
        bool GetExclusiveFullScreenState() const override;
        bool SetExclusiveFullScreenState(bool fullScreenState) override;
        //////////////////////////////////////////////////////////////////////////

        void ConfigureDisplayMode(const RHI::SwapChainDimensions& dimensions);
        void EnsureColorSpace(const DXGI_COLOR_SPACE_TYPE& colorSpace);
        void DisableHdr();
        void SetHDRMetaData(float maxOutputNits, float minOutputNits, float maxContentLightLevel, float maxFrameAverageLightLevel);

        static const uint32_t InvalidColorSpace = 0xFFFFFFFE;
        DXGI_COLOR_SPACE_TYPE m_colorSpace = static_cast<DXGI_COLOR_SPACE_TYPE>(InvalidColorSpace);

        Ptr<IDXGISwapChainX> m_swapChain;
        bool m_isInFullScreenExclusiveState = false; //!< Was SetFullscreenState used to enter full screen exclusive state?
        bool m_isTearingSupported = false; //!< Is tearing support available for full screen borderless windowed mode?

    };
}