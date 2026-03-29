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
#include <ID3D12Factory.h>
#include <MemoryView.h>
#include <Command/CommandQueueContext.h>
#include <Resource/Image/Image.h>
#include <3rdParty/D3D12MA/D3D12MemAlloc.h>

namespace Spark::RHI::DX12
{
    Device& SwapChain::GetDevice() const
    {
        return static_cast<Device&>(RHI::SwapChain::GetDevice());
    }

    RHI::ResultCode SwapChain::InitInternal(RHI::Device& deviceBase, RHI::CommandQueue& commandQueue, const RHI::SwapChainDescriptor& descriptor, RHI::SwapChainDimensions* nativeDimensions)
    {
        // Check whether tearing support is available for full screen borderless windowed mode.
        Device& device = static_cast<Device&>(deviceBase);
        BOOL allowTearing = FALSE;
        IDXGIFactoryX* dxgiFactory = device.GetPhysicalDevice().GetFactory();
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

        ASSERT(commandQueue.GetHardwareQueueClass() == RHI::HardwareQueueClass::Graphics, "Must use graphics command queue to create swapchain");
        CommandQueue& dx12CommandQueue = static_cast<CommandQueue&>(commandQueue);
        HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
            dx12CommandQueue.GetNativeQueue(),
            reinterpret_cast<HWND>(window),
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChainPtr);

        if (SUCCEEDED(hr))
        {
            ComPtr<IDXGISwapChainX> swapChainX;
            swapChainPtr->QueryInterface(IID_PPV_ARGS(swapChainX.GetAddressOf()));
            m_swapChain = swapChainX.Get();
            
            // ConfigureDisplayMode(*nativeDimensions);
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
                parentFactory->MakeWindowAssociation(reinterpret_cast<HWND>(window), DXGI_MWA_NO_ALT_ENTER);
            }

            return RHI::ResultCode::Success;
        }
        return RHI::ResultCode::Fail;
    }

    void SwapChain::ShutdownInternal()
    {
        // We must exit exclusive full screen mode before shutting down.
        // Safe to call even if not in the exclusive full screen state.
        m_swapChain->SetFullscreenState(0, nullptr);
        m_swapChain = nullptr;
    }

    uint32_t SwapChain::PresentInternal()
    {
        if (m_swapChain)
        {
            // It is recommended to always pass the DXGI_PRESENT_ALLOW_TEARING flag when it is supported, even when presenting in windowed mode.
            // But it cannot be used in an application that is currently in full screen exclusive mode, set by calling SetFullscreenState(TRUE).
            // To use this flag in full screen Win32 apps the application should present to a fullscreen borderless window and disable automatic
            // ALT+ENTER fullscreen switching using IDXGIFactory::MakeWindowAssociation (please see implementation of SwapChain::InitInternal).
            // UINT presentFlags = (m_isTearingSupported && !m_isInFullScreenExclusiveState) ? DXGI_PRESENT_ALLOW_TEARING : 0;
            HRESULT hresult = m_swapChain->Present(GetDescriptor().m_verticalSyncInterval, 0);

            ASSERT(SUCCEEDED(hresult), "SwapChain present error.");

            return (GetCurrentImageIndex() + 1) % GetImageCount();
        }

        return GetCurrentImageIndex();
    }

    RHI::ResultCode SwapChain::InitImageInternal(const InitImageRequest& request)
    {
        Device& device = GetDevice();

        ComPtr<ID3D12Resource> resource;
        HRESULT hr = m_swapChain->GetBuffer(request.m_imageIndex, IID_PPV_ARGS(resource.GetAddressOf()));
        ASSERT(SUCCEEDED(hr), "SwapChain Get buffer failed.");

        D3D12_RESOURCE_ALLOCATION_INFO allocationInfo;
        D3D12_RESOURCE_DESC resourceDesc;
        ConvertImageDescriptor(request.m_descriptor, resourceDesc);
        allocationInfo = device.GetDX12Device()->GetResourceAllocationInfo(0, 1, &resourceDesc);

        Image& image = static_cast<Image&>(*request.m_image);

        image.m_memoryView = MemoryView(resource.Get(), MemoryViewType::Image, 0, allocationInfo.SizeInBytes, allocationInfo.Alignment);
        image.GenerateSubresourceLayouts();
        // Overwrite m_initialAttachmentState because Swapchain images are created with D3D12_RESOURCE_STATE_COMMON state
        image.SetAttachmentState(D3D12_RESOURCE_STATE_COMMON);

        return RHI::ResultCode::Success;
    }

    void SwapChain::ShutdownImageInternal(RHI::Image& imageBase)
    {
        Image& image = static_cast<Image&>(imageBase);

        image.m_memoryView = {};
    }

    RHI::ResultCode SwapChain::ResizeInternal(const RHI::SwapChainDimensions& dimensions, RHI::SwapChainDimensions* nativeDimensions)
    {
        // 等待command完成
        //GetDevice().WaitForIdle();

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        m_swapChain->GetDesc(&swapChainDesc);
        if (SUCCEEDED(m_swapChain->ResizeBuffers(
                dimensions.m_imageCount,
                dimensions.m_imageWidth,
                dimensions.m_imageHeight,
                swapChainDesc.BufferDesc.Format,
                swapChainDesc.Flags)))
        {
            if (nativeDimensions)
            {
                *nativeDimensions = dimensions;
            }
            // ConfigureDisplayMode(dimensions);

            // Check whether SetFullscreenState was used to enter full screen exclusive state.
            BOOL fullscreenState;
            m_isInFullScreenExclusiveState = SUCCEEDED(m_swapChain->GetFullscreenState(&fullscreenState, nullptr)) && fullscreenState;

            return RHI::ResultCode::Success;
        }

        return RHI::ResultCode::Fail;
    }

    bool SwapChain::IsExclusiveFullScreenPreferred() const
    {
        return !m_isTearingSupported;
    }

    bool SwapChain::GetExclusiveFullScreenState() const
    {
        return m_isInFullScreenExclusiveState;
    }

    bool SwapChain::SetExclusiveFullScreenState(bool fullScreenState)
    {
        if (m_swapChain)
        {
            m_swapChain->SetFullscreenState(fullScreenState, nullptr);
        }

        // The above call to SetFullscreenState will ultimately result in
        // SwapChain:ResizeInternal being called above where we set this.
        return (fullScreenState == m_isInFullScreenExclusiveState);
    }
}