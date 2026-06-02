#include "ShaderAssetCompiler.h"

#include <combaseapi.h>
#include <d3d12shader.h>
#include <dxcapi.h>

#include <string>

#include <EASTL/algorithm.h>
#include <EASTL/unique_ptr.h>

#include <Log/ILogSystem.h>
#include <Resource/Common/CommonAssetLoader.h>

namespace Spark::Resource
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

    namespace
    {
        std::wstring ToWide(const eastl::string& s)
        {
            return std::wstring(s.begin(), s.end());
        }

        //! DXC include handler that routes #include through the asset search
        //! paths (same resolution as BinaryAssetLoader) and records each
        //! included file as a build dependency. Quote-includes resolve relative
        //! to the including file (DXC combines the path against the source name
        //! we pass before calling LoadSource); angle / fallback includes resolve
        //! against the -I search roots. Stack-owned for the duration of Compile.
        class ShaderIncludeHandler : public IDxcIncludeHandler
        {
        public:
            ShaderIncludeHandler(IDxcUtils* utils, const eastl::vector<eastl::string>& searchPaths)
                : m_utils(utils)
            {
                m_loader.SetSearchPaths(searchPaths);
            }

            HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) override
            {
                if (!ppIncludeSource)
                {
                    return E_POINTER;
                }
                *ppIncludeSource = nullptr;

                const eastl::string path = Normalize(pFilename);

                auto data = m_loader.LoadFile(path);
                if (!data)
                {
                    return E_FAIL; // DXC turns this into an "include not found" diagnostic
                }
                const auto& bytes = static_cast<BinaryAssetData*>(data.get())->GetBytes();

                IDxcBlobEncoding* blob = nullptr;
                HRESULT hr = m_utils->CreateBlobFromPinned(
                    bytes.data(), static_cast<UINT32>(bytes.size()), DXC_CP_UTF8, &blob);
                if (FAILED(hr))
                {
                    return hr;
                }

                // CreateBlobFromPinned does not copy — keep the bytes alive for
                // the handler's whole lifetime (covers every stage Compile).
                m_keepAlive.push_back(eastl::move(data));
                if (eastl::find(m_dependencies.begin(), m_dependencies.end(), path) == m_dependencies.end())
                {
                    m_dependencies.push_back(path);
                }

                *ppIncludeSource = blob;
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
            {
                if (!ppvObject)
                {
                    return E_POINTER;
                }
                if (riid == __uuidof(IDxcIncludeHandler) || riid == __uuidof(IUnknown))
                {
                    *ppvObject = this;
                    return S_OK;
                }
                *ppvObject = nullptr;
                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
            ULONG STDMETHODCALLTYPE Release() override { return 1; }

            const eastl::vector<eastl::string>& GetDependencies() const { return m_dependencies; }

        private:
            static eastl::string Normalize(LPCWSTR w)
            {
                eastl::string s;
                for (const wchar_t* p = w; p && *p; ++p)
                {
                    const char c = static_cast<char>(*p);
                    s.push_back(c == '\\' ? '/' : c);
                }
                while (s.size() >= 2 && s[0] == '.' && s[1] == '/')
                {
                    s.erase(0, 2);
                }
                return s;
            }

            IDxcUtils*                                  m_utils;
            BinaryAssetLoader                           m_loader;
            eastl::vector<eastl::unique_ptr<AssetData>> m_keepAlive;
            eastl::vector<eastl::string>                m_dependencies;
        };

        RHI::ShaderInputType MapInputType(D3D_SHADER_INPUT_TYPE type)
        {
            switch (type)
            {
            case D3D_SIT_TEXTURE:
            case D3D_SIT_UAV_RWTYPED:
                return RHI::ShaderInputType::Image;
            case D3D_SIT_SAMPLER:
                return RHI::ShaderInputType::Sampler;
            default:
                return RHI::ShaderInputType::Buffer;
            }
        }

        RHI::ShaderInputBufferAccess MapBufferAccess(D3D_SHADER_INPUT_TYPE type)
        {
            switch (type)
            {
            case D3D_SIT_CBUFFER:
                return RHI::ShaderInputBufferAccess::Constant;
            case D3D_SIT_UAV_RWTYPED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                return RHI::ShaderInputBufferAccess::ReadWrite;
            default:
                return RHI::ShaderInputBufferAccess::Read;
            }
        }

        RHI::ShaderInputBufferType MapBufferType(D3D_SHADER_INPUT_TYPE type)
        {
            switch (type)
            {
            case D3D_SIT_CBUFFER:
                return RHI::ShaderInputBufferType::Constant;
            case D3D_SIT_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                return RHI::ShaderInputBufferType::Structured;
            case D3D_SIT_BYTEADDRESS:
            case D3D_SIT_UAV_RWBYTEADDRESS:
                return RHI::ShaderInputBufferType::Raw;
            case D3D_SIT_TBUFFER:
                return RHI::ShaderInputBufferType::Typed;
            default:
                return RHI::ShaderInputBufferType::Unknown;
            }
        }

        RHI::ShaderInputImageAccess MapImageAccess(D3D_SHADER_INPUT_TYPE type)
        {
            switch (type)
            {
            case D3D_SIT_UAV_RWTYPED:
                return RHI::ShaderInputImageAccess::ReadWrite;
            default:
                return RHI::ShaderInputImageAccess::Read;
            }
        }

        RHI::ShaderInputImageType MapImageType(D3D_SRV_DIMENSION dim)
        {
            switch (dim)
            {
            case D3D_SRV_DIMENSION_TEXTURE1D:        return RHI::ShaderInputImageType::Image1D;
            case D3D_SRV_DIMENSION_TEXTURE1DARRAY:   return RHI::ShaderInputImageType::Image1DArray;
            case D3D_SRV_DIMENSION_TEXTURE2D:        return RHI::ShaderInputImageType::Image2D;
            case D3D_SRV_DIMENSION_TEXTURE2DARRAY:   return RHI::ShaderInputImageType::Image2DArray;
            case D3D_SRV_DIMENSION_TEXTURE2DMS:      return RHI::ShaderInputImageType::Image2DMultisample;
            case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY: return RHI::ShaderInputImageType::Image2DMultisampleArray;
            case D3D_SRV_DIMENSION_TEXTURE3D:        return RHI::ShaderInputImageType::Image3D;
            case D3D_SRV_DIMENSION_TEXTURECUBE:      return RHI::ShaderInputImageType::ImageCube;
            case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: return RHI::ShaderInputImageType::ImageCubeArray;
            default:                                 return RHI::ShaderInputImageType::Unknown;
            }
        }
    }

    eastl::unique_ptr<AssetData> ShaderAssetCompiler::Compile(const AssetId& id, AssetData& rawData,
        const eastl::vector<eastl::string>& searchPaths)
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

        // #include resolution: search the asset roots plus the compiled shader's
        // own directory, so both `#include "X"` (sibling) and root-relative
        // includes resolve consistently with the asset system.
        eastl::vector<eastl::string> includeSearch = searchPaths;
        {
            const eastl::string& resolved = binaryData.GetResolvedPath();
            const auto slash = resolved.find_last_of("/\\");
            if (slash != eastl::string::npos)
            {
                includeSearch.push_back(resolved.substr(0, slash));
            }
        }
        ShaderIncludeHandler includeHandler(m_utils, includeSearch);

        // Wide-string arg storage must outlive the Compile() calls below.
        // Source name gives DXC the base dir for relative #include + diagnostics.
        const std::wstring wSourceName = ToWide(id.GetPath());
        eastl::vector<std::wstring> wIncludeDirs;
        wIncludeDirs.reserve(includeSearch.size());
        for (const auto& dir : includeSearch)
        {
            wIncludeDirs.push_back(ToWide(dir));
        }

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
                LOG_ERROR("DXC: Failed to create source blob for {}", id.GetPath().c_str());
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

            // Input filename: DXC uses its directory as the base for relative
            // `#include "..."` and reports it in diagnostics. The buffer above
            // is the actual source; this is metadata only.
            args.push_back(wSourceName.c_str());

            // Include search roots for angle / fallback includes.
            for (const auto& wDir : wIncludeDirs)
            {
                args.push_back(L"-I");
                args.push_back(wDir.c_str());
            }

            if (m_backend == ShaderBackend::SPIRV)
            {
                args.push_back(L"-spirv");
            }

#ifndef NODEBUG
            args.push_back(L"-Zi");     // 调试信息
            args.push_back(L"-Od");     // 禁用优化
#endif

            IDxcResult* compileResult = nullptr;
            hr = m_compiler->Compile(
                &sourceBuffer,
                args.data(),
                static_cast<UINT32>(args.size()),
                &includeHandler,
                IID_PPV_ARGS(&compileResult));

            sourceBlob->Release();

            if (FAILED(hr))
            {
                LOG_ERROR("DXC: Compile call failed for {} [{}]",
                    id.GetPath().c_str(), entry.entryPoint.c_str());
                return nullptr;
            }

            HRESULT compileStatus;
            compileResult->GetStatus(&compileStatus);

            if (FAILED(compileStatus))
            {
                IDxcBlobUtf8* errors = nullptr;
                compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
                if (errors && errors->GetStringLength() > 0)
                {
                    LOG_ERROR("DXC: Shader compile error [{}:{}]:\n{}",
                        id.GetPath().c_str(),
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
                    id.GetPath().c_str(), entry.entryPoint.c_str());
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

            // ---- 提取该 stage 的反射信息 ----
            {
                IDxcBlob* reflectionBlob = nullptr;
                hr = compileResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr);

                if (SUCCEEDED(hr) && reflectionBlob && reflectionBlob->GetBufferSize() > 0)
                {
                    DxcBuffer refBuffer{};
                    refBuffer.Ptr = reflectionBlob->GetBufferPointer();
                    refBuffer.Size = reflectionBlob->GetBufferSize();
                    refBuffer.Encoding = 0;

                    ID3D12ShaderReflection* shaderReflection = nullptr;
                    hr = m_utils->CreateReflection(&refBuffer, IID_PPV_ARGS(&shaderReflection));

                    if (SUCCEEDED(hr) && shaderReflection)
                    {
                        ShaderStageReflection stageReflection;

                        D3D12_SHADER_DESC shaderDesc;
                        shaderReflection->GetDesc(&shaderDesc);

                        // --- 提取 cbuffer 及内部变量布局 ---
                        for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
                        {
                            ID3D12ShaderReflectionConstantBuffer* cb = shaderReflection->GetConstantBufferByIndex(i);
                            D3D12_SHADER_BUFFER_DESC cbDesc;
                            cb->GetDesc(&cbDesc);

                            if (cbDesc.Type != D3D_CT_CBUFFER)
                            {
                                continue;
                            }

                            ShaderConstantBufferReflection cbRefl;
                            cbRefl.m_name = cbDesc.Name ? cbDesc.Name : "";
                            cbRefl.m_byteSize = cbDesc.Size;

                            // 通过资源绑定时查找 register / space
                            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
                            if (SUCCEEDED(shaderReflection->GetResourceBindingDescByName(cbDesc.Name, &bindDesc)))
                            {
                                cbRefl.m_registerId = bindDesc.BindPoint;
                                cbRefl.m_spaceId = bindDesc.Space;
                            }

                            cbRefl.m_variables.reserve(cbDesc.Variables);
                            for (UINT v = 0; v < cbDesc.Variables; ++v)
                            {
                                ID3D12ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
                                D3D12_SHADER_VARIABLE_DESC varDesc;
                                var->GetDesc(&varDesc);

                                ShaderConstantVariableReflection varRefl;
                                varRefl.m_name = varDesc.Name ? varDesc.Name : "";
                                varRefl.m_byteOffset = varDesc.StartOffset;
                                varRefl.m_byteSize = varDesc.Size;

                                // ---- stride-aware fields ----
                                // 把"数组-of-矩阵"展开成统一的 (elementCount, elementByteSize, elementStride)
                                // 三元组，让 ShaderInputConstant::SetData 自动处理 HLSL CB 16-byte 对齐。
                                ID3D12ShaderReflectionType* varType = var->GetType();
                                D3D12_SHADER_TYPE_DESC typeDesc{};
                                if (varType && SUCCEEDED(varType->GetDesc(&typeDesc)))
                                {
                                    // 标量大小：HLSL CB 里 int/uint/bool/float 都是 4B，double 是 8B。
                                    const uint32_t scalarSize =
                                        (typeDesc.Type == D3D_SVT_DOUBLE) ? 8u : 4u;

                                    const uint32_t arrayLen =
                                        (typeDesc.Elements > 0) ? typeDesc.Elements : 1u;

                                    uint32_t innerCount = 1;
                                    uint32_t innerSize  = scalarSize;
                                    if (typeDesc.Class == D3D_SVC_MATRIX_COLUMNS)
                                    {
                                        // column-major: array of Columns 列，每列 Rows 个标量
                                        innerCount = typeDesc.Columns;
                                        innerSize  = typeDesc.Rows * scalarSize;
                                    }
                                    else if (typeDesc.Class == D3D_SVC_MATRIX_ROWS)
                                    {
                                        // row-major: array of Rows 行，每行 Columns 个标量
                                        innerCount = typeDesc.Rows;
                                        innerSize  = typeDesc.Columns * scalarSize;
                                    }
                                    else
                                    {
                                        // SCALAR / VECTOR: Rows=1, Columns=N → 单一 element
                                        innerCount = 1;
                                        innerSize  = typeDesc.Rows * typeDesc.Columns * scalarSize;
                                    }

                                    varRefl.m_elementCount    = arrayLen * innerCount;
                                    varRefl.m_elementByteSize = innerSize;

                                    const bool hasMultiplicity = (arrayLen > 1) || (innerCount > 1);
                                    varRefl.m_elementStride =
                                        hasMultiplicity ? 16u : innerSize;
                                }
                                else
                                {
                                    // Fallback：当成单个原子 blob
                                    varRefl.m_elementCount    = 1;
                                    varRefl.m_elementByteSize = varDesc.Size;
                                    varRefl.m_elementStride   = varDesc.Size;
                                }

                                cbRefl.m_variables.push_back(eastl::move(varRefl));
                            }

                            stageReflection.m_cbuffers.push_back(eastl::move(cbRefl));
                        }

                        // --- 提取非 cbuffer 的资源绑定 (SRV / UAV / Sampler) ---
                        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
                        {
                            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
                            shaderReflection->GetResourceBindingDesc(i, &bindDesc);

                            if (bindDesc.Type == D3D_SIT_CBUFFER || bindDesc.Type == D3D_SIT_TBUFFER)
                            {
                                continue;
                            }

                            ShaderResourceBindingReflection resRefl;
                            resRefl.m_name = bindDesc.Name ? bindDesc.Name : "";
                            resRefl.m_type = MapInputType(bindDesc.Type);
                            resRefl.m_registerId = bindDesc.BindPoint;
                            resRefl.m_count = bindDesc.BindCount;
                            resRefl.m_spaceId = bindDesc.Space;
                            resRefl.m_bufferAccess = MapBufferAccess(bindDesc.Type);
                            resRefl.m_bufferType = MapBufferType(bindDesc.Type);
                            resRefl.m_imageAccess = MapImageAccess(bindDesc.Type);
                            resRefl.m_imageType = MapImageType(bindDesc.Dimension);

                            stageReflection.m_resources.push_back(eastl::move(resRefl));
                        }

                        // --- Compute: 提取线程组尺寸 ---
                        if (entry.stage == RHI::ShaderStage::Compute)
                        {
                            shaderReflection->GetThreadGroupSize(
                                &stageReflection.m_threadGroupSizeX,
                                &stageReflection.m_threadGroupSizeY,
                                &stageReflection.m_threadGroupSizeZ);
                        }

                        result->AddStageReflection(entry.stage, eastl::move(stageReflection));

                        shaderReflection->Release();
                    }

                    reflectionBlob->Release();
                }
            }

            shaderBlob->Release();
            compileResult->Release();
        }

        // Record #include build dependencies (deduped across stages) for a
        // future cook-cache / hot-reload to key off of.
        for (const auto& dep : includeHandler.GetDependencies())
        {
            result->AddDependency(dep);
        }

        return result;
    }
}
