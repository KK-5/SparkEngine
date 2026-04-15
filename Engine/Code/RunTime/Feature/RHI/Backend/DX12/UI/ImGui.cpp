#include "ImGui.h"

#include <backends/imgui_impl_dx12.h>

#include <ID3D12Factory.h>

#include <Device/Device.h>
#include <SwapChain/SwapChain.h>
#include <Command/CommandQueue.h>
#include <Command/CommandList.h>
#include <Descriptor/DescriptorContext.h>

#include <Conversions.h>

namespace Spark::RHI::DX12
{

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
            m_userData.descriptorContext->ReleaseDescriptor(handle);
        }
        m_userData.allocatedHandles.clear();
        m_userData.descriptorContext = nullptr;
    }

    void ImGui::NewFrame()
    {
        ImGui_ImplDX12_NewFrame();
    }

    void ImGui::RenderDrawData(ImDrawData* drawData, RHI::CommandList* commandList)
    {
        // TODO: Need to pass ID3D12GraphicsCommandList* from the caller
        auto dx12CommandList = static_cast<CommandList*>(commandList);
        ImGui_ImplDX12_RenderDrawData(drawData, dx12CommandList->GetCommandList());
    }

    void ImGui::SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
    {
        auto* userData = static_cast<ImGuiDescriptorUserData*>(info->UserData);
        DescriptorHandle handle = userData->descriptorContext->AllocateHandle(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        *outCpu = userData->descriptorContext->GetCpuNativeHandle(handle);
        *outGpu = userData->descriptorContext->GetGpuNativeHandle(handle);
        userData->allocatedHandles[outGpu->ptr] = handle;
    }

    void ImGui::SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        auto* userData = static_cast<ImGuiDescriptorUserData*>(info->UserData);
        auto it = userData->allocatedHandles.find(gpu.ptr);
        if (it != userData->allocatedHandles.end())
        {
            userData->descriptorContext->ReleaseDescriptor(it->second);
            userData->allocatedHandles.erase(it);
        }
    }
}
