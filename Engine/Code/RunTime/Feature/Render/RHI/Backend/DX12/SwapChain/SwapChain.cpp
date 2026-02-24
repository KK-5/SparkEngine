/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "SwapChain.h"

#include <Log/SpdLogSystem.h>

#include <Conversions.h>
#include <Device/Device.h>

namespace Spark::RHI::DX12
{
    Device& SwapChain::GetDevice() const
    {
        return static_cast<Device&>(RHI::SwapChain::GetDevice());
    }

    RHI::ResultCode SwapChain::InitInternal(RHI::Device& deviceBase, const RHI::SwapChainDescriptor& descriptor, RHI::SwapChainDimensions* nativeDimensions)
    {
        // Check whether tearing support is available for full screen borderless windowed mode.
        Device& device = static_cast<Device&>(deviceBase);
        BOOL allowTearing = FALSE;
        ComPtr<IDXGIFactoryX> dxgiFactory;
        HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
        ASSERT(SUCCEEDED(result), "CreateDXGIFactory2 failed");
        m_isTearingSupported = SUCCEEDED(dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) && allowTearing;

        if (nativeDimensions)
        {
            *nativeDimensions = descriptor.m_dimensions;
        }
        const uint32_t SwapBufferCount = eastl::max(RHI::Limits::Device::MinSwapChainImages, deviceBase.GetDescriptor().m_frameCountMax);

        DXGI_SWAP_CHAIN_DESCX swapChainDesc = {};
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferCount = SwapBufferCount;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.Width = descriptor.m_dimensions.m_imageWidth;
        swapChainDesc.Height = descriptor.m_dimensions.m_imageHeight;
        swapChainDesc.Format = ConvertFormat(descriptor.m_dimensions.m_imageFormat);
        swapChainDesc.Scaling = ConvertScaling(descriptor.m_scalingMode);
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        if (m_isTearingSupported)
        {
            // It is recommended to always use the tearing flag when it is available.
            swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        IUnknown* window = reinterpret_cast<IUnknown*>(descriptor.m_window);
        ComPtr<IDXGISwapChain1> swapChainPtr;

        HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
            m_commandQueueContext.GetCommandQueue(RHI::HardwareQueueClass::Graphics).GetPlatformQueue(),
            reinterpret_cast<HWND>(descriptor.m_window),
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChainPtr);


        RHI::ResultCode result = device.CreateSwapChain(reinterpret_cast<IUnknown*>(descriptor.m_window.GetIndex()), swapChainDesc, m_swapChain);
        if (result == RHI::ResultCode::Success)
        {
            ConfigureDisplayMode(*nativeDimensions);

            // According to various docs (and the D3D12Fulscreen sample), when tearing is supported
            // a borderless full screen window is always preferred over exclusive full screen mode.
            //
            // - https://devblogs.microsoft.com/directx/demystifying-full-screen-optimizations/
            // - https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays
            //
            // So we have modelled our full screen support on the D3D12Fulscreen sample by choosing
            // the best full screen mode to use based on whether tearing is supported by the device.
            //
            // It would be possible to allow a choice between these different full screen modes,
            // but we have chosen not to given that guidance for DX12 appears to be discouraging
            // the use of exclusive full screen mode, and because no other platforms support it.
            if (m_isTearingSupported)
            {
                // To use tearing in full screen Win32 apps the application should present to a fullscreen borderless window and disable automatic
                // ALT+ENTER fullscreen switching using IDXGIFactory::MakeWindowAssociation (see also implementation of SwapChain::PresentInternal).
                // You must call the MakeWindowAssociation method after the creation of the swap chain, and on the factory object associated with the
                // target HWND swap chain, which you can guarantee by calling the IDXGIObject::GetParent method on the swap chain to locate the factory.
                IDXGIFactoryX* parentFactory = nullptr;
                m_swapChain->GetParent(__uuidof(IDXGIFactoryX), (void **)&parentFactory);
                device.AssertSuccess(parentFactory->MakeWindowAssociation(reinterpret_cast<HWND>(window), DXGI_MWA_NO_ALT_ENTER));
            }
        }
        return result;

    }
}