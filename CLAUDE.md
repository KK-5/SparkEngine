# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

CMake project, Visual Studio 17 2022 (x64) is the primary generator. Presets are defined in `CMakePresets.json`.

```bash
# Configure (Debug, MSVC)
cmake --preset windows-msvc-debug
# Build
cmake --build build --config Debug
# Or via preset
cmake --build --preset debug

# Ninja Debug (faster iteration)
cmake --preset windows-ninja-debug
cmake --build --preset ninja-debug

# Run tests (after build)
ctest --test-dir build -C Debug
# Run a single gtest
./bin/Debug/SparkCoreTest.exe --gtest_filter=ECSTest.*
./bin/Debug/SparkAssetTest.exe --gtest_filter=ImageAssetTests.*
```

Test source toggles live in `Engine/Code/Test/Core/CMakeLists.txt` (`Reflection_TESTS`, `ECS_TESTS`, etc.) — flip the relevant `option()` to disable a suite at configure time.

`bin/<config>/` is `CMAKE_RUNTIME_OUTPUT_DIRECTORY`. The `copy_dxc_dlls` custom target copies `dxcompiler.dll` / `dxil.dll` from `Engine/3rdParty/DXC/bin/x64/` next to executables — this runs as part of ALL_BUILD, so DXC is available by the time samples link.

`SandBox/Program/` builds two RHI sample executables (`HelloTriangle`, `DrawShape`) controlled by `BUILD_RHI_HELLOTRIANGLE` / `BUILD_RHI_DRAWSHAPE` options. They consume `SHADER_ASSET_DIR` as a compile definition pointing at `SandBox/Asset/`.

ASan build: use `windows-msvc-debug-asan` preset (sets `/fsanitize=address` on Debug C/CXX flags).

## Architecture

### Module layering

```
SparkCore  ──────────────►  ECS, EBus, Service, Math, Object, Log, Reflection
   ▲
   ├─ SparkRHI            ── RHI abstraction (backend-agnostic, models Vulkan-strictness)
   │     └─ SparkRHI_DX12 ── DX12 backend
   │
   ├─ SparkAssetManager   ── Asset/Resource/* types (textures, shaders)
   ├─ SparkInput / SparkWindow / SparkPlatform / SparkUI
   └─ SparkRender         ── render system, render graph, passes (consumes SparkRHI)

SparkRuntime              ── aggregator that links everything for the engine binary
SandBox/Program/*         ── sample executables that link SparkRHI directly (without full runtime)
Engine/Code/Test/*        ── gtest/gmock test executables
```

Each module's `CMakeLists.txt` exposes its directory via `target_include_directories(... PUBLIC ...)`. Critically, **SparkRHI exposes its parent (`Code/RunTime/Feature/`) as the include root**, so RHI users include via `<RHI/...>`. SparkRHI_DX12 exposes its own directory, so backend-internal headers are included as `<DX12.h>`, `<Conversions.h>`, `<ID3D12Factory.h>`, etc. (no `DX12/` prefix). The DX12 backend's own internal files use `#include "Foo.h"` for same-directory siblings.

### System lifecycle

`Spark::ISystem` (`Core/ECS/ISystem.h`) defines the `Init()` / `Shutdown()` shape with `InitInternal()` / `ShutdownInternal()` virtuals — every long-lived subsystem inherits this. `SparkEngine::SetUp()` (`Engine/Code/RunTime/Engine.cpp`) constructs systems via `CreateSystem<T>()` which returns `SystemUniquePtr<T>` (auto-shutdown deleter), then calls `Init()` in dependency order. `Run()` drives the frame via `TickBus::Broadcast(OnTick, dt)`.

`Service<T>` (`Core/Service/Service.h`) is a typed singleton registry. Subsystems that need cross-module access inherit `Service<IFoo>::Handler` — registration happens in the `Handler` constructor. Resolve via `auto p = Service<IFoo>::Get();` (returns `nullptr` if unregistered). Examples: `Service<RHIInterface>`, `Service<ID3D12FactoryInterface>`, `Service<IWindowSystem>`. **Service does not own lifetime** — registration is a weak pointer.

`EBus<Interface>` (`Core/EBus/EBus.h`) is the codebase's pub-sub. Frame-level RHI events flow over `FrameEventBus` (`OnFrameBegin` / `OnFrameCompileBegin` / `OnFrameCompileEnd` / `OnFrameEnd`); tick over `TickBus`.

### ECS

Built on `entt`. `BasicContext<EntityType>` (`Core/ECS/BasicContext.h`) wraps `entt::registry`. The world uses `WorldContext = BasicContext<Entity>`. `ExecuteContext<EntityType>` is a thread-local context stack; `ExecuteContextGuard` scopes a push/pop. `WorldExecuteContext::Push(world)` is called in `SparkEngine::SetUp()` before any system runs, so systems can call `WorldExecuteContext::Current()` without plumbing the context through every API.

The render system uses a separate ECS context: `RHIContext = BasicContext<RHIHandle>` (`Render/Pass/RHIContext.h`). RHI resources, views, and pass attachments live in this context as entities — `TransientTag`, `ImportedTag`, `ResourceName`, `ImagePassAttachment`, etc. are components on `RHIHandle` entities.

### RHI design (read this before touching `Feature/RHI/`)

The RHI is loosely modeled on O3DE/Atom but **explicitly diverges** wherever Atom's choices conflict with Vulkan strictness or carry framework bloat. Key rules:

- **Cross-backend abstraction follows the stricter backend.** When DX12 and Vulkan have asymmetric semantics, RHI signatures align with whichever is more constrained — typically Vulkan. The looser backend (DX12) drops what it doesn't need; the stricter one cannot synthesize what's missing. Concrete example: `BeginRenderPass` / `EndRenderPass` carries `AttachmentLoadStoreAction` + `RenderAttachmentLayout` because Vulkan requires it (and TBDR mobile bandwidth depends on correct loadOp/storeOp), even though DX12's `OMSetRenderTargets` doesn't.

- **Never defer cross-backend correctness to "when Vulkan is added".** Any data structure, descriptor, or validation that exists in the RHI abstraction layer must be designed to be correct for all intended backends from the moment it is written — even if only the DX12 backend is currently implemented. "We'll fix it when we add Vulkan" is not acceptable: deferred compatibility debt compounds and forces breaking API changes later. If the correct multi-backend design is not yet clear, resolve the design question first before writing the code.

- **No render-layer concepts inside RHI.** RHI types and APIs must not mention `Attachment`, `AttachmentId`, `Scope`, `Pass`, `RenderGraph`, or `Frame Graph`. These belong to `SparkRender` above. RHI takes opaque integers (e.g. `timelinePosition`), descriptors, and queue masks. `Scope` in particular is an O3DE term that has been removed.

- **EASTL replaces AZStd** throughout. `Ptr<T> = eastl::intrusive_ptr<T>` (`Core/Base.h`), `UniquePtr<T> = eastl::unique_ptr<T>`. ComPtr (Microsoft) is used only at the DX12 boundary; convert to `Ptr<>` for storage via `pComPtr.Get()`.

- **`Service<T>::Get()` replaces `AZ::Interface<T>::Get()`.**

- **`m_registry` in `ResourcePool` is deprecated.** Resource ownership tracking now flows through `Resource::m_pool` (single back-pointer set in `ResourcePool::Register`). `~Resource()` self-routes destruction via that pointer. New pools should not depend on iterating the registry.

#### Resource pools

`ResourcePool` (`RHI/Resource/ResourcePool.h`) is the abstract base. `Init()` takes a `BackendMethod` lambda — derived pools wrap it as: validate descriptor → save descriptor → call `InitInternal(device, descriptor)`. See `BufferPool::Init` / `ImagePool::Init` for the canonical pattern.

`OnFrameBegin` / `OnFrameCompileBegin` / `OnFrameEnd` are the frame hooks. Validation flag `m_isProcessingFrame` is only set under `Validation::isEnabled` — do not depend on it for state-machine logic; track your own batch state if needed.

Backends extend a sub-base (`BufferPool` → `DX12::BufferPool`) and override the `*Internal` virtuals. Each DX12 pool owns its own `D3D12MA::Allocator` plus a `D3D12MAReleaseQueue m_releaseQueue` for `D3D12MA::Allocation` deferred release. For `ID3D12Object` deferred release, **do not maintain a per-pool queue** — use `Service<ID3D12FactoryInterface>::Get()->QueueForRelease(device, ptr)` which routes to the shared `D3D12ObjReleaseQueue` owned by `ID3D12Factory`. Latency for both queues is `device.GetDescriptor().m_frameCountMax`.

#### DX12 backend specifics

- `D3D12MA::Allocator` is per-pool (matches existing `BufferPool` / `ImagePool` pattern), not a singleton.
- `MemoryView` (`Backend/DX12/MemoryView.h`) wraps `(D3D12MA::Allocation*, offset, size, alignment, type)` — both Image and Buffer hold one.
- Aliased / placed resources go through `Allocator::AllocateMemory` (raw heap) + `CreateAliasingResource` (placed resource at offset). `CreateResource` is for committed allocations only; `ALLOCATION_FLAG_CAN_ALIAS` is for `CreateResource`'s output, not `AllocateMemory`'s.
- Heap-tier 2 lets buffers and all texture types share one heap (`D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES`). Tier 1 requires three category-specific heaps.
- Heap alignment for any-resource heaps is `D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT` (4 MB) when MSAA RT/DS may be placed; otherwise 64 KB.

### Render system

`Render::RenderSystem` (`Feature/Render/RenderSystem.h`) is a `TickBus::Handler` with `RenderSystemTickOrder`. It owns the `RHIContext`, a `Pipeline`, and a `RenderGraphBuilder`. Render-graph compilation (`Feature/Render/RenderGraph/`) lives entirely above the RHI; passes register attachments through the builder, the compiler (currently empty stub) is responsible for topo-sort, transient resource allocation, and barrier compilation. `PassBarriers` (in `Pass/Component/PassComponents.h`) holds the compiled `RHI::ImageBarrier` / `RHI::BufferBarrier` lists per pass.

## Conventions

- C++17, `/permissive-`, warnings-as-errors disabled (`/WX-`). MSVC-only build today.
- `NOMINMAX` and `MATH_BACKEND_GLM` are global compile definitions.
- Math backend is GLM (`Engine/3rdParty/glm/`). Vector/matrix types live in `Core/Math/`.
- Use `eastl::*` containers, not `std::*` (with the obvious exceptions for OS handles, `std::shared_mutex`, `std::chrono` already used in some places).
- Logging: `LOG_ERROR`, `LOG_WARN` etc. from `<Log/SpdLogSystem.h>`. Use `[ClassName]` prefix in messages by convention.
- Validation gates: wrap debug-only checks in `if (Validation::isEnabled) { ... }` (RHI) or `ASSERT(...)`. Never use `assert()` directly.
- Resource handles in the render layer are `RHIHandle` (entt entity); `NullHandle` is the sentinel.
- Same-directory headers in DX12 backend use `"Foo.h"`; cross-module use `<RHI/...>`, `<Math/...>`, etc.
- **Always use braces for `if` / `for` / `while` / `do` bodies, even when the body is a single statement.** No `if (cond) return;` or `for (x : y) doThing(x);` one-liners. This keeps diffs clean when adding statements and avoids the dangling-else / wrong-scope class of bugs.
