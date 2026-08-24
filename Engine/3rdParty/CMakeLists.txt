set(THIRD_PARTY_FOLDER "3rdParty")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(NOT TARGET EnTT)
    Message(STATUS "Import entt")
    add_subdirectory(entt)
    set_target_properties(EnTT PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/entt)
endif()

if(NOT TARGET spdlog)
    Message(STATUS "Import spdlog")
    add_subdirectory(spdlog)
    set_target_properties(spdlog PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/spdlog)
endif()

if(NOT TARGET EASTL)
    Message(STATUS "Import eastl")
    add_subdirectory(EASTL)
    target_compile_definitions(EASTL PUBLIC EASTL_STD_ITERATOR_CATEGORY_ENABLED=1)
    set_target_properties(EASTL PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/EASTL)
endif()

if(NOT TARGET glfw)
    Message(STATUS "Import glfw")
    add_subdirectory(glfw)
    set_target_properties(glfw PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/glfw)
endif()

if(NOT TARGET ImGui)
    Message(STATUS "Import imgui")
    add_subdirectory(imgui)
    set_target_properties(ImGui PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/imgui)
endif()

if(NOT TARGET glm)
    Message(STATUS "Import glm")
    add_subdirectory(glm)
    set_target_properties(glm PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/glm)
endif()

if(NOT TARGET dxc)
    Message(STATUS "Import DXC")
    add_library(dxc SHARED IMPORTED GLOBAL)
    set_target_properties(dxc PROPERTIES
        IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/DXC/lib/x64/dxcompiler.lib"
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/DXC/bin/x64/dxcompiler.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/DXC/inc"
    )
endif()

if(NOT TARGET stb)
    Message(STATUS "Import stb")
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

if(NOT TARGET nlohmann_json)
    Message(STATUS "Import nlohmann/json")
    # Header-only. Public headers may only include <nlohmann/json_fwd.hpp>; the 25k-line
    # <nlohmann/json.hpp> belongs in .cpp files, so its compile cost stays bounded to the
    # handful of translation units that actually serialize.
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann/include)
endif()

if(NOT TARGET nanosvg)
    Message(STATUS "Import nanosvg")
    add_library(nanosvg STATIC nanosvg/nanosvg.cpp)
    target_include_directories(nanosvg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/nanosvg/src)
    set_target_properties(nanosvg PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/nanosvg)
endif()

if(NOT TARGET ktx)
    Message(STATUS "Import KTX-Software (libktx)")

    # Strip everything we don't need: tools, tests, docs, language bindings,
    # KTX1 legacy, GPU upload helpers (we have our own RHI), OpenCL accel.
    set(KTX_FEATURE_STATIC_LIBRARY ON  CACHE BOOL "" FORCE)
    set(KTX_FEATURE_TOOLS          OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_TESTS          OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_DOC            OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_JNI            OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_PY             OFF CACHE BOOL "" FORCE)
    # KTX1 cannot be cleanly disabled: texture.obj's dispatcher unconditionally
    # references KTX1 constructors, so OFF leads to LNK2019 at exe link time.
    set(KTX_FEATURE_KTX1           ON  CACHE BOOL "" FORCE)
    set(KTX_FEATURE_KTX2           ON  CACHE BOOL "" FORCE)
    set(KTX_FEATURE_VK_UPLOAD      OFF CACHE BOOL "" FORCE)
    set(KTX_FEATURE_GL_UPLOAD      OFF CACHE BOOL "" FORCE)
    set(BASISU_SUPPORT_OPENCL      OFF CACHE BOOL "" FORCE)
    set(KTX_WERROR                 OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING              OFF CACHE BOOL "" FORCE)

    add_subdirectory(KTX-Software)

    foreach(_ktx_target ktx ktx_read objUtil astcenc-avx2-static astcenc-sse4.1-static astcenc-sse2-static astcenc-none-static)
        if(TARGET ${_ktx_target})
            set_target_properties(${_ktx_target} PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/ktx)
        endif()
    endforeach()
endif()

if(NOT TARGET fastgltf)
    Message(STATUS "Import fastgltf")
    # simdjson is auto-downloaded into fastgltf/deps/simdjson on first configure.
    set(FASTGLTF_ENABLE_TESTS    OFF CACHE BOOL "" FORCE)
    set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(FASTGLTF_ENABLE_DOCS     OFF CACHE BOOL "" FORCE)
    add_subdirectory(fastgltf)
    set_target_properties(fastgltf PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/fastgltf)
endif()

if(NOT TARGET meshoptimizer)
    Message(STATUS "Import meshoptimizer")
    set(MESHOPT_BUILD_DEMO        OFF CACHE BOOL "" FORCE)
    set(MESHOPT_BUILD_GLTFPACK    OFF CACHE BOOL "" FORCE)
    set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(MESHOPT_INSTALL           OFF CACHE BOOL "" FORCE)
    set(MESHOPT_WERROR            OFF CACHE BOOL "" FORCE)
    add_subdirectory(meshoptimizer)
    set_target_properties(meshoptimizer PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/meshoptimizer)
endif()

if(NOT TARGET mikktspace)
    Message(STATUS "Import MikkTSpace")
    add_library(mikktspace STATIC
        MikkTSpace/mikktspace.c
        MikkTSpace/mikktspace.h
    )
    target_include_directories(mikktspace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/MikkTSpace)
    set_target_properties(mikktspace PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/MikkTSpace)
endif()

if(BUILD_TESTS)
    Message(STATUS "Import gtest")
    option(BUILD_GMOCK "Builds the googlemock subproject" ON)
    add_subdirectory(googletest)
    set_target_properties(gtest PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/googletest)
endif()