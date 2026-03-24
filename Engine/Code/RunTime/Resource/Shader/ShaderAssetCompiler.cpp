#include "ShaderAssetCompiler.h"

#include <combaseapi.h>
#include <dxcapi.h>

#include <Log/SpdLogSystem.h>
#include <Resource/Common/CommonAssetLoader.h>

namespace Spark::Asset
{
    ShaderAssetCompiler::ShaderAssetCompiler(ShaderBackend backend)
        : m_backend(backend)
    {
        if (!InitDxc())
        {
            LOG_ERROR("Failed to initialize DXC compiler");
        }
    }

    ShaderAssetCompiler::~ShaderAssetCompiler()
    {
        if (m_compiler)
        {
            m_compiler->Release();
            m_compiler = nullptr;
        }
        if (m_utils)
        {
            m_utils->Release();
            m_utils = nullptr;
        }
    }

    void ShaderAssetCompiler::AddStageEntry(ShaderStageEntry entry)
    {
        m_stageEntries.push_back(eastl::move(entry));
    }

    bool ShaderAssetCompiler::InitDxc()
    {
        HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils));
        if (FAILED(hr))
        {
            return false;
        }

        hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler));
        if (FAILED(hr))
        {
            m_utils->Release();
            m_utils = nullptr;
            return false;
        }

        return true;
    }

    eastl::unique_ptr<AssetData> ShaderAssetCompiler::Compile(const AssetId& id, AssetData& rawData)
    {
        if (!m_compiler || !m_utils)
        {
            LOG_ERROR("DXC not initialized");
            return nullptr;
        }

        auto& binaryData = static_cast<BinaryAssetData&>(rawData);
        const auto& sourceBytes = binaryData.GetBytes();

        auto result = eastl::make_unique<ShaderAssetData>();
        result->SetBackend(m_backend);
        result->SetSourcePath(binaryData.GetResolvedPath());

        for (const auto& entry : m_stageEntries)
        {
            // 创建源码 blob
            IDxcBlobEncoding* sourceBlob = nullptr;
            HRESULT hr = m_utils->CreateBlobFromPinned(
                sourceBytes.data(),
                static_cast<UINT32>(sourceBytes.size()),
                DXC_CP_UTF8,
                &sourceBlob);

            if (FAILED(hr))
            {
                LOG_ERROR("DXC: Failed to create source blob for {}", id.GetName().GetStringView().data());
                return nullptr;
            }

            DxcBuffer sourceBuffer{};
            sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
            sourceBuffer.Size = sourceBlob->GetBufferSize();
            sourceBuffer.Encoding = DXC_CP_UTF8;

            // 构建编译参数
            // 将 eastl::string 转为 wstring 供 DXC 使用
            auto toWide = [](const eastl::string& str) -> std::wstring {
                return std::wstring(str.begin(), str.end());
            };

            std::wstring wEntryPoint = toWide(entry.entryPoint);
            std::wstring wTargetProfile = toWide(entry.targetProfile);

            eastl::vector<LPCWSTR> args;
            args.push_back(L"-E");
            args.push_back(wEntryPoint.c_str());
            args.push_back(L"-T");
            args.push_back(wTargetProfile.c_str());

            if (m_backend == ShaderBackend::SPIRV)
            {
                args.push_back(L"-spirv");
            }

#ifndef NODEBUG
            args.push_back(L"-Zi");     // 调试信息
            args.push_back(L"-Od");     // 禁用优化
#endif

            // 编译
            IDxcResult* compileResult = nullptr;
            hr = m_compiler->Compile(
                &sourceBuffer,
                args.data(),
                static_cast<UINT32>(args.size()),
                nullptr,    // include handler
                IID_PPV_ARGS(&compileResult));

            sourceBlob->Release();

            if (FAILED(hr))
            {
                LOG_ERROR("DXC: Compile call failed for {} [{}]",
                    id.GetName().GetStringView().data(), entry.entryPoint.c_str());
                return nullptr;
            }

            // 检查编译状态
            HRESULT compileStatus;
            compileResult->GetStatus(&compileStatus);

            if (FAILED(compileStatus))
            {
                IDxcBlobUtf8* errors = nullptr;
                compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
                if (errors && errors->GetStringLength() > 0)
                {
                    LOG_ERROR("DXC: Shader compile error [{}:{}]:\n{}",
                        id.GetName().GetStringView().data(),
                        entry.entryPoint.c_str(),
                        errors->GetStringPointer());
                }
                if (errors) errors->Release();
                compileResult->Release();
                return nullptr;
            }

            IDxcBlob* shaderBlob = nullptr;
            compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);

            if (!shaderBlob || shaderBlob->GetBufferSize() == 0)
            {
                LOG_ERROR("DXC: Empty output for {} [{}]",
                    id.GetName().GetStringView().data(), entry.entryPoint.c_str());
                if (shaderBlob) shaderBlob->Release();
                compileResult->Release();
                return nullptr;
            }

            ShaderStageBytecode bytecode;
            bytecode.stage = entry.stage;
            bytecode.entryPoint = entry.entryPoint;
            bytecode.bytecode.resize(shaderBlob->GetBufferSize());
            memcpy(bytecode.bytecode.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

            result->AddStageBytecode(eastl::move(bytecode));

            shaderBlob->Release();
            compileResult->Release();
        }

        return result;
    }
}
