#include "ImGui.h"

#include <backends/imgui_impl_dx12.h>

#include <ID3D12Factory.h>

#include <Device/Device.h>
#include <SwapChain/SwapChain.h>
#include <Command/CommandQueue.h>
#include <Command/CommandList.h>
#include <Descriptor/DescriptorContext.h>
#include <Resource/Image/ImageView.h>

#include <Conversions.h>

namespace Spark::RHI::DX12
{
    ImGui::~ImGui()
    {
        Shutdown();
    }

    RHI::ResultCode ImGui::InitInternal(RHI::Device& deviceBase, RHI::CommandQueue& commandQueue, [[maybe_unused]] const ImGuiDescriptor& desc)
    {
        ASSERT(Service<ID3D12FactoryInterface>::Get(), "ID3D12Factory is valid");

        auto& device = static_cast<Device&>(deviceBase);
        auto& dx12Queue = static_cast<CommandQueue&>(commandQueue);
        auto& descriptorContext = Service<ID3D12FactoryInterface>::Get()->AcquireDescriptorContext(device);

        m_userData.descriptorContext = &descriptorContext;

        ImGui_ImplDX12_InitInfo info;
        info.Device = device.GetDX12Device();
        info.NumFramesInFlight = deviceBase.GetDescriptor().m_frameCountMax;
        info.CommandQueue = dx12Queue.GetNativeQueue();
        info.RTVFormat = ConvertFormat(desc.m_rtvFormat);
        info.DSVFormat = ConvertFormat(desc.m_dsvFormat);
        info.SrvDescriptorHeap = descriptorContext.GetPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>().GetNativeHeap();
        info.UserData = &m_userData;
        info.SrvDescriptorAllocFn = &ImGui::SrvDescriptorAlloc;
        info.SrvDescriptorFreeFn = &ImGui::SrvDescriptorFree;

        ImGui_ImplDX12_Init(&info);

        return RHI::ResultCode::Success;
    }

    void ImGui::Shutdown()
    {
        ImGui_ImplDX12_Shutdown();

        // Release all allocated descriptors
        for (auto& [ptr, handle] : m_userData.allocatedHandles)
        {
            m_userData.descriptorContext->ReleaseDescriptorTable(handle);
        }
        m_userData.allocatedHandles.clear();

        // Release registered texture batches
        for (auto& batch : m_registeredBatches)
        {
            m_userData.descriptorContext->ReleaseDescriptorTable(batch.m_table);
        }
        m_registeredBatches.clear();

        m_userData.descriptorContext = nullptr;
    }

    void ImGui::NewFrame()
    {
        ImGui_ImplDX12_NewFrame();
    }

    void ImGui::RenderDrawData(ImDrawData* drawData, RHI::CommandList* commandList)
    {
        auto dx12CommandList = static_cast<CommandList*>(commandList);
        ImGui_ImplDX12_RenderDrawData(drawData, dx12CommandList->GetCommandList());
    }

    void ImGui::SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
    {
        auto* userData = static_cast<ImGuiDescriptorUserData*>(info->UserData);
        // SHADER_VISIBLE 堆上不允许创建单独的handle，即使只有一个也应该使用table
        DescriptorTable table = userData->descriptorContext->AllocateTable<
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>(1);
        *outCpu = userData->descriptorContext->GetCpuNativeHandleForTable(table);
        *outGpu = userData->descriptorContext->GetGpuNativeHandleForTable(table);
        userData->allocatedHandles[outGpu->ptr] = table;
    }

    void ImGui::SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        auto* userData = static_cast<ImGuiDescriptorUserData*>(info->UserData);
        auto it = userData->allocatedHandles.find(gpu.ptr);
        if (it != userData->allocatedHandles.end())
        {
            userData->descriptorContext->ReleaseDescriptorTable(it->second);
            userData->allocatedHandles.erase(it);
        }
    }

    void ImGui::RegisterTextures(
        const eastl::vector<const RHI::ImageView*>& imageViews,
        eastl::vector<ImTextureID>& outTextureIds)
    {
        const uint32_t count = static_cast<uint32_t>(imageViews.size());
        outTextureIds.assign(count, ImTextureID_Invalid);

        if (count == 0 || !m_userData.descriptorContext)
        {
            return;
        }

        // Batch-allocate a contiguous descriptor table from the shader-visible heap.
        DescriptorTable table = m_userData.descriptorContext->AllocateTable<
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE>(count);

        // Collect source CPU descriptor handles from each DX12 ImageView.
        eastl::vector<DescriptorHandle> srcHandles;
        srcHandles.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            auto* dx12View = static_cast<const ImageView*>(imageViews[i]);
            srcHandles.push_back(dx12View->GetReadDescriptor());
        }

        // Copy all SRV descriptors into ImGui's shader-visible heap at once.
        m_userData.descriptorContext->UpdateDescriptorTableRange(
            table,
            srcHandles.data(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Compute individual ImTextureID values from the contiguous table.
        for (uint32_t i = 0; i < count; ++i)
        {
            DescriptorHandle handle = table[i];
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
                m_userData.descriptorContext->GetGpuNativeHandle(handle);
            outTextureIds[i] = (ImTextureID)gpuHandle.ptr;
        }

        m_registeredBatches.push_back({table, count});
    }
}
