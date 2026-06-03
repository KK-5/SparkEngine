#pragma once

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <RHI/UI/ImGui.h>
#include <Descriptor/Descriptor.h>
#include <Descriptor/DescriptorContext.h>

struct ImGui_ImplDX12_InitInfo;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_GPU_DESCRIPTOR_HANDLE;

namespace Spark::RHI::DX12
{
    class Device;
    class CommandQueue;

    struct ImGuiDescriptorUserData
    {
        DescriptorContext* descriptorContext = nullptr;
        eastl::unordered_map<uint64_t, DescriptorTable> allocatedHandles;
    };

    struct RegisteredTextureBatch
    {
        DescriptorTable m_table;
        uint32_t        m_count = 0;
    };

    class ImGui final : public RHI::ImGui
    {
    public:
        ~ImGui();

        void Shutdown() override;
        void NewFrame() override;
        void RenderDrawData(ImDrawData* drawData, RHI::CommandList* commandList) override;

        void RegisterTextures(
            const eastl::vector<const RHI::ImageView*>& imageViews,
            eastl::vector<ImTextureID>& outTextureIds) override;

    private:
        RHI::ResultCode InitInternal(RHI::Device& device, RHI::CommandQueue& commandQueue, const ImGuiDescriptor& desc) override;

        static void SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
        static void SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

        ImGuiDescriptorUserData m_userData;
        eastl::vector<RegisteredTextureBatch> m_registeredBatches;
    };
}