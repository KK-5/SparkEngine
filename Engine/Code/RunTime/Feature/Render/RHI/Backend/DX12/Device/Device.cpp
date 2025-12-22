#include "Device.h"

#include <Log/SpdLogSystem.h>
#include <Math/Bit.h>
#include <ValidationLayer.h>
#include <DX12.h>
#include <Conversions.h>
#include <3rdParty/D3D12MA/D3D12MemAlloc.h>

namespace Spark::RHI::DX12
{
    void EnableD3DDebugLayer()
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
    }

    void EnableGPUBasedValidation()
    {
        ComPtr<ID3D12Debug1> debugController1;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController1))))
        {
            debugController1->SetEnableGPUBasedValidation(TRUE);
            debugController1->SetEnableSynchronizedCommandQueueValidation(TRUE);
        }

        ComPtr<ID3D12Debug2> debugController2;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController2))))
        {
            debugController2->SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_NONE);
        }
    }

    void EnableDebugDeviceFeatures(ComPtr<ID3D12DeviceX>& dx12Device)
    {
        ComPtr<ID3D12DebugDevice2> debugDevice;
        if (SUCCEEDED(dx12Device->QueryInterface(debugDevice.GetAddressOf())))
        {
            D3D12_DEBUG_FEATURE featureFlags{ D3D12_DEBUG_FEATURE_ALLOW_BEHAVIOR_CHANGING_DEBUG_AIDS };
            debugDevice->SetDebugParameter(D3D12_DEBUG_DEVICE_PARAMETER_FEATURE_FLAGS, &featureFlags, sizeof(featureFlags));
            featureFlags = D3D12_DEBUG_FEATURE_CONSERVATIVE_RESOURCE_STATE_TRACKING;
            debugDevice->SetDebugParameter(D3D12_DEBUG_DEVICE_PARAMETER_FEATURE_FLAGS, &featureFlags, sizeof(featureFlags));
        }
    }

    void EnableBreakOnD3DError(ComPtr<ID3D12DeviceX>& dx12Device)
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(dx12Device->QueryInterface(infoQueue.GetAddressOf())))
        {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
        }
    }

    void AddDebugFilters(ComPtr<ID3D12DeviceX>& dx12Device, RHI::ValidationMode validationMode)
    {
        eastl::vector<D3D12_MESSAGE_SEVERITY> enabledSeverities;
        eastl::vector<D3D12_MESSAGE_ID> disabledMessages;

        // These severities should be seen all the time
        enabledSeverities.push_back(D3D12_MESSAGE_SEVERITY_CORRUPTION);
        enabledSeverities.push_back(D3D12_MESSAGE_SEVERITY_ERROR);
        enabledSeverities.push_back(D3D12_MESSAGE_SEVERITY_WARNING);
        enabledSeverities.push_back(D3D12_MESSAGE_SEVERITY_MESSAGE);

        if (validationMode == RHI::ValidationMode::Verbose)
        {
            // Verbose only filters
            enabledSeverities.push_back(D3D12_MESSAGE_SEVERITY_INFO);
        }

        ////// O3DE settings

        // [GFX TODO][ATOM-4573] - We keep getting this warning when reading from query buffers on a job thread
        // while a command queue thread is submitting a command list that is using the same buffer, but in a
        // different region. We should add validation elsewhere to make sure that multi-threaded access continues to
        // be valid and possibly find a way to restore this warning to catch other cases that could be invalid.
        disabledMessages.push_back(D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_GPU_WRITTEN_READBACK_RESOURCE_MAPPED);

        // Disabling this message because it is harmless, yet it overwhelms the Editor log when the D3D Debug Layer is enabled.
        // D3D12 WARNING: ID3D12CommandList::DrawIndexedInstanced: Element [6] in the current Input Layout's declaration references input slot 6,
        //                but there is no Buffer bound to this slot. This is OK, as reads from an empty slot are defined to return 0.
        //                It is also possible the developer knows the data will not be used anyway.
        //                This is only a problem if the developer actually intended to bind an input Buffer here.
        //                [ EXECUTION WARNING #202: COMMAND_LIST_DRAW_VERTEX_BUFFER_NOT_SET]
        disabledMessages.push_back(D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_VERTEX_BUFFER_NOT_SET);

        // We disable these warnings as the our current implementation of Pipeline Library will trigger these warnings unknowingly. For example
        // it will always first try to load a pso from pipelinelibrary triggering D3D12_MESSAGE_ID_LOADPIPELINE_NAMENOTFOUND (for the first time) before storing the PSO in a library.
        // Similarly when we merge multiple pipeline libraries (in multiple threads) we may trigger D3D12_MESSAGE_ID_STOREPIPELINE_DUPLICATENAME as it is possible to save
        // a PSO already saved in the main library. 
        disabledMessages.push_back(D3D12_MESSAGE_ID_LOADPIPELINE_NAMENOTFOUND);
        disabledMessages.push_back(D3D12_MESSAGE_ID_STOREPIPELINE_DUPLICATENAME);

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(dx12Device->QueryInterface(infoQueue.GetAddressOf())))
        {
            D3D12_INFO_QUEUE_FILTER filter{ };
            filter.AllowList.NumSeverities = static_cast<UINT>(enabledSeverities.size());
            filter.AllowList.pSeverityList = enabledSeverities.data();
            filter.DenyList.NumIDs = static_cast<UINT>(disabledMessages.size());
            filter.DenyList.pIDList = disabledMessages.data();

            // Clear out the existing filters since we're taking full control of them
            infoQueue->PushEmptyStorageFilter();

            [[maybe_unused]] HRESULT addedOk = infoQueue->AddStorageFilterEntries(&filter);
            assert(addedOk == S_OK, "D3DInfoQueue AddStorageFilterEntries failed");

            infoQueue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_MESSAGE, "D3D12 Debug Filters setup");
        }
    }

    RHI::ResultCode Device::InitD3d12maAllocator()
    {
        // Create D3d12ma allocator
        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
        desc.pDevice = m_dx12Device.get();
        desc.pAdapter = m_dxgiAdapter.get();

        D3D12MA::Allocator* dx12MemAlloc = nullptr;
        if (FAILED(D3D12MA::CreateAllocator(&desc, &dx12MemAlloc)))
        {
            LOG_ERROR("[DX12 Device] Failed to initialize the D3D12MemoryAllocator.");
            return RHI::ResultCode::Fail;
        }
        m_dx12MemAlloc = dx12MemAlloc;
        return RHI::ResultCode::Success;
    }

    void Device::InitFeatures()
    {
        m_features.m_geometryShader = true;
        m_features.m_computeShader = true;
        m_features.m_independentBlend = true;
        m_features.m_dualSourceBlending = true;
        D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2;
        GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &options2, sizeof(options2));
        m_features.m_customSamplePositions =
            options2.ProgrammableSamplePositionsTier != D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED;
        //m_features.m_queryTypesMask[static_cast<uint32_t>(RHI::HardwareQueueClass::Graphics)] = RHI::QueryTypeFlags::All;
        //m_features.m_queryTypesMask[static_cast<uint32_t>(RHI::HardwareQueueClass::Compute)] = RHI::QueryTypeFlags::PipelineStatistics | RHI::QueryTypeFlags::Timestamp;
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3;
        GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3));
        if (options3.CopyQueueTimestampQueriesSupported)
        {
            //m_features.m_queryTypesMask[static_cast<uint32_t>(RHI::HardwareQueueClass::Copy)] = RHI::QueryTypeFlags::Timestamp;
        }
        m_features.m_predication = true;
        m_features.m_occlusionQueryPrecise = true;
        //m_features.m_indirectCommandTier = RHI::IndirectCommandTiers::Tier2;
        m_features.m_indirectDrawCountBufferSupported = true;
        m_features.m_indirectDispatchCountBufferSupported = true;
        m_features.m_indirectDrawStartInstanceLocationSupported = true;
        m_features.m_signalFenceFromCPU = true;
        //m_features.m_crossDeviceFences = true;
        //m_features.m_crossDeviceDeviceMemory = true;

        // DXGI_SCALING_ASPECT_RATIO_STRETCH is only compatible with CreateSwapChainForCoreWindow or CreateSwapChainForComposition,
        // not Win32 window handles and associated methods (cannot find an MSDN source for that)
        // Source: https://stackoverflow.com/questions/58586223/d3d11-createswapchainforhwnd-fails-with-either-dxgi-error-invalid-call-or-e-inva
        // Create swapchain would fail if uses DXGI_SCALING_ASPECT_RATIO_STRETCH
        m_features.m_swapchainScalingFlags = RHI::ScalingFlags::Stretch;
                    
        D3D12_FEATURE_DATA_D3D12_OPTIONS options;
        GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
        // DX12's tile resource implementation uses undefined swizzle tile layout which only requires tier 1
        m_features.m_tiledResource = options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_1;

        // Check support of wive operation
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel;
        shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_0;
        if (FAILED(GetDevice()->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
        {
            LOG_WARN("[DX12 Device] Failed to check feature D3D12_FEATURE_SHADER_MODEL");
            m_features.m_waveOperation = false;
        }
        else
        {
            m_features.m_waveOperation = shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_0;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5;
        GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
        m_features.m_rayTracing = options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

        m_features.m_float16 = (options.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT) != 0;

        m_features.m_unboundedArrays = true;

        D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6;
        GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6));
        switch (options6.VariableShadingRateTier)
        {
        case D3D12_VARIABLE_SHADING_RATE_TIER::D3D12_VARIABLE_SHADING_RATE_TIER_1:
            {
                m_features.m_shadingRateTypeMask = RHI::ShadingRateTypeFlags::PerDraw;
                m_features.m_shadingRateMask = static_cast<ShadingRateFlags>(
                    static_cast<uint32_t>(RHI::ShadingRateFlags::Rate1x1) |
                    static_cast<uint32_t>(RHI::ShadingRateFlags::Rate1x2) |
                    static_cast<uint32_t>(RHI::ShadingRateFlags::Rate2x1) |
                    static_cast<uint32_t>(RHI::ShadingRateFlags::Rate2x2)
                );                   
            }
            break;
        case D3D12_VARIABLE_SHADING_RATE_TIER::D3D12_VARIABLE_SHADING_RATE_TIER_2:
            {
                m_features.m_shadingRateTypeMask = static_cast<ShadingRateTypeFlags>(
                    static_cast<uint32_t>(RHI::ShadingRateTypeFlags::PerDraw) |
                    static_cast<uint32_t>(RHI::ShadingRateTypeFlags::PerRegion) |
                    static_cast<uint32_t>(RHI::ShadingRateTypeFlags::PerPrimitive)
                );
                m_features.m_shadingRateMask = static_cast<ShadingRateFlags>(
                    static_cast<uint32_t>(RHI::ShadingRateFlags::Rate1x1) |
                    static_cast<uint32_t>(RHI::ShadingRateFlags::Rate1x2) |
                    static_cast<uint32_t>(RHI::ShadingRateFlags::Rate2x1) |
                    static_cast<uint32_t>(RHI::ShadingRateFlags::Rate2x2)
                );
                m_features.m_dynamicShadingRateImage = true;
            }
            break;
        default:
            break;
        }

        if (options6.AdditionalShadingRatesSupported)
        {
           uint32_t hadingRateMask = 
                static_cast<uint32_t>(RHI::ShadingRateFlags::Rate2x4) |
                static_cast<uint32_t>(RHI::ShadingRateFlags::Rate4x2) |
                static_cast<uint32_t>(RHI::ShadingRateFlags::Rate4x4);
            
             m_features.m_shadingRateMask = static_cast<ShadingRateFlags>(
                static_cast<uint32_t>(m_features.m_shadingRateMask) |
                hadingRateMask
             );
        }

        m_limits.m_shadingRateTileSize = RHI::Size(options6.ShadingRateImageTileSize, options6.ShadingRateImageTileSize, 1);

        m_limits.m_maxImageDimension1D = D3D12_REQ_TEXTURE1D_U_DIMENSION;
        m_limits.m_maxImageDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_limits.m_maxImageDimension3D = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        m_limits.m_maxImageDimensionCube = D3D12_REQ_TEXTURECUBE_DIMENSION;
        m_limits.m_maxImageArraySize = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        m_limits.m_minConstantBufferViewOffset = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        m_limits.m_maxIndirectDrawCount = static_cast<uint32_t>(-1);
        m_limits.m_maxIndirectDispatchCount = static_cast<uint32_t>(-1);
        m_limits.m_maxConstantBufferSize = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 4u * 4u; // 4096 vectors * 4 values per vector * 4 bytes per value
        m_limits.m_maxBufferSize = D3D12_REQ_RESOURCE_SIZE_IN_MEGABYTES_EXPRESSION_C_TERM * (1024u * 1024u); // 2048 MB
    }

    RHI::ResultCode Device::InitInternal(RHI::PhysicalDevice& physicalDevice)
    {
        PhysicalDevice& dx12PhysicalDevice = static_cast<PhysicalDevice&>(physicalDevice);
        RHI::ValidationMode validationMode = RHI::validationMode;

        if (validationMode != RHI::ValidationMode::Disabled)
        {
            EnableD3DDebugLayer();
            if (validationMode == RHI::ValidationMode::GPU)
            {
                EnableGPUBasedValidation();
            }

            // DRED has a perf cost on some drivers/hw so only enable it if RHI validation is enabled.
#ifdef __ID3D12DeviceRemovedExtendedDataSettings1_INTERFACE_DEFINED__
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> pDredSettings;
#else
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
#endif
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings))))
            {
                // Turn on auto-breadcrumbs and page fault reporting.
                pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
#ifdef __ID3D12DeviceRemovedExtendedDataSettings1_INTERFACE_DEFINED__
                pDredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
#endif
            }
        }

        ComPtr<ID3D12DeviceX> dx12Device;
        if (FAILED(D3D12CreateDevice(dx12PhysicalDevice.GetAdapter(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(dx12Device.GetAddressOf()))))
        {
            LOG_ERROR("[DX12 Device] Failed to initialize the device. Check the debug layer for more info.");
            return RHI::ResultCode::Fail;
        }

        if (validationMode != RHI::ValidationMode::Disabled)
        {
            EnableDebugDeviceFeatures(dx12Device);
            EnableBreakOnD3DError(dx12Device);
            AddDebugFilters(dx12Device, validationMode);
        }

        m_dx12Device = dx12Device.Get();
        m_dxgiFactory = dx12PhysicalDevice.GetFactory();
        m_dxgiAdapter = dx12PhysicalDevice.GetAdapter();

        RHI::ResultCode resultCode = InitD3d12maAllocator();
        if (resultCode != RHI::ResultCode::Success)
        {
            return resultCode;
        }

        InitFeatures();

        return RHI::ResultCode::Success;
    }

    void Device::ShutdownInternal()
    {
        //m_allocationInfoCache.Clear();

        //m_stagingMemoryAllocator.Shutdown();

        //m_pipelineLayoutCache.Shutdown();

        //m_descriptorContext = nullptr;

        //m_releaseQueue.Shutdown();

        //m_D3d12maReleaseQueue.Shutdown();
        m_dx12MemAlloc = nullptr;

        m_dxgiFactory = nullptr;
        m_dxgiAdapter = nullptr;

        if (validationMode != RHI::ValidationMode::Disabled)
        {
            ID3D12DebugDevice* dx12DebugDevice = nullptr;
            if (m_dx12Device)
            {
                m_dx12Device->QueryInterface(&dx12DebugDevice);
            }
            if (dx12DebugDevice)
            {
                dx12DebugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
                dx12DebugDevice->Release();
            }
        }
        m_dx12Device = nullptr;
    }

    RHI::ResultCode Device::BeginFrameInternal()
    {
        static uint32_t frameIndex = 0;
        if (m_dx12MemAlloc)
        {
            m_dx12MemAlloc->SetCurrentFrameIndex(++frameIndex);
        }
        //m_commandQueueContext.Begin();
    }

    void Device::EndFrameInternal()
    {
        //m_commandQueueContext.End();

        //m_commandListAllocator.Collect();

        //m_descriptorContext->GarbageCollect();

        //m_stagingMemoryAllocator.GarbageCollect();

        //m_releaseQueue.Collect();
        
        //m_D3d12maReleaseQueue.Collect();
    }

    void Device::WaitForIdleInternal()
    {
        //m_commandQueueContext.WaitForIdle();
        //m_releaseQueue.Collect(true);

        //m_D3d12maReleaseQueue.Collect(true);

    }

    RHI::ResultCode Device::InitializeLimits()
    {
        /*
        m_allocationInfoCache.SetInitFunction([](auto& cache) { cache.set_capacity(64); });

        {
            ReleaseQueue::Descriptor releaseQueueDescriptor;
            releaseQueueDescriptor.m_collectLatency = m_descriptor.m_frameCountMax;
            m_releaseQueue.Init(releaseQueueDescriptor);

            D3d12maReleaseQueue::Descriptor D3d12maReleaseQueueDescriptor;
            D3d12maReleaseQueueDescriptor.m_collectLatency = m_descriptor.m_frameCountMax;
            D3d12maReleaseQueueDescriptor.m_collectFunction = &D3d12maRelease;
            m_D3d12maReleaseQueue.Init(D3d12maReleaseQueueDescriptor);
        }

        m_descriptorContext = AZStd::make_shared<DescriptorContext>();

        RHI::ConstPtr<RHI::PlatformLimitsDescriptor> rhiDescriptor = m_descriptor.m_platformLimitsDescriptor;
        RHI::ConstPtr<PlatformLimitsDescriptor> platLimitsDesc = azrtti_cast<const PlatformLimitsDescriptor*>(rhiDescriptor);
        AZ_Assert(platLimitsDesc != nullptr, "Missing PlatformLimits config file for DX12 backend");
        m_descriptorContext->Init(m_dx12Device.get(), platLimitsDesc);

        {
            CommandListAllocator::Descriptor commandListAllocatorDescriptor;
            commandListAllocatorDescriptor.m_device = this;
            commandListAllocatorDescriptor.m_frameCountMax = m_descriptor.m_frameCountMax;
            commandListAllocatorDescriptor.m_descriptorContext = m_descriptorContext;
            m_commandListAllocator.Init(commandListAllocatorDescriptor);
        }

        {
            StagingMemoryAllocator::Descriptor allocatorDesc;
            allocatorDesc.m_device = this;

            allocatorDesc.m_mediumPageSizeInBytes = static_cast<uint32_t>(platLimitsDesc->m_platformDefaultValues.m_mediumStagingBufferPageSizeInBytes);
            allocatorDesc.m_largePageSizeInBytes = static_cast<uint32_t>(platLimitsDesc->m_platformDefaultValues.m_largestStagingBufferPageSizeInBytes);
            allocatorDesc.m_collectLatency = m_descriptor.m_frameCountMax;
            m_stagingMemoryAllocator.Init(allocatorDesc);
        }

        m_pipelineLayoutCache.Init(*this);

        m_commandQueueContext.Init(*this);

        m_asyncUploadQueue.Init(*this, AsyncUploadQueue::Descriptor(platLimitsDesc->m_platformDefaultValues.m_asyncQueueStagingBufferSizeInBytes));

        m_samplerCache.SetCapacity(SamplerCacheCapacity);
        */

        return RHI::ResultCode::Success;
    }

    void Device::FillFormatsCapabilitiesInternal(FormatCapabilitiesList& formatsCapabilities)
    {
        for (uint32_t i = 0; i < formatsCapabilities.size(); ++i)
        {
            RHI::FormatCapabilities& flags = formatsCapabilities[i];
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = ConvertFormat(static_cast<RHI::Format>(i), false);
            GetDevice()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
            flags = RHI::FormatCapabilities::None;

            if (CheckBitsAll(support.Support1, D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER))
            {
                flags |= RHI::FormatCapabilities::VertexBuffer;
            }

            if (CheckBitsAll(support.Support1, D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER))
            {
                flags |= RHI::FormatCapabilities::IndexBuffer;
            }

            if (CheckBitsAll(support.Support1, D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
            {
                flags |= RHI::FormatCapabilities::RenderTarget;
            }

            if (CheckBitsAll(support.Support1, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
            {
                flags |= RHI::FormatCapabilities::DepthStencil;
            }

            if (CheckBitsAll(support.Support1, D3D12_FORMAT_SUPPORT1_BLENDABLE))
            {
                flags |= RHI::FormatCapabilities::Blend;
            }

            if (CheckBitsAll(support.Support1, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE))
            {
                flags |= RHI::FormatCapabilities::Sample;
            }

            if (CheckBitsAll(support.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))
            {
                flags |= RHI::FormatCapabilities::TypedLoadBuffer;
            }

            if (CheckBitsAll(support.Support2, D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
            {
                flags |= RHI::FormatCapabilities::TypedStoreBuffer;
            }

            if (CheckBitsAll(support.Support2, D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD))
            {
                flags |= RHI::FormatCapabilities::AtomicBuffer;
            }
        }

        formatsCapabilities[static_cast<uint32_t>(RHI::Format::R8_UINT)] |= RHI::FormatCapabilities::ShadingRate;
    }

    ID3D12DeviceX* Device::GetDevice()
    {
        return m_dx12Device.get();
    }
}