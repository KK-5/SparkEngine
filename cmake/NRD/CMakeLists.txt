cmake_minimum_required(VERSION 3.30)

project(SparkNRDSDK LANGUAGES NONE)

if(NOT WIN32)
    message(FATAL_ERROR "This NRD SDK project packages Windows/DX12 libraries.")
endif()

include(ExternalProject)

set(SPARK_NRD_SOURCE_DIR "" CACHE PATH "Path to an existing NRD source tree")
if(SPARK_NRD_SOURCE_DIR STREQUAL "")
    set(nrd_source_args
        GIT_REPOSITORY https://github.com/NVIDIA-RTX/NRD.git
        GIT_TAG v4.18.0
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
    )
else()
    get_filename_component(nrd_source_dir "${SPARK_NRD_SOURCE_DIR}" REALPATH)
    if(NOT EXISTS "${nrd_source_dir}/CMakeLists.txt" OR NOT EXISTS "${nrd_source_dir}/Include/NRD.h")
        message(FATAL_ERROR "SPARK_NRD_SOURCE_DIR must point to an NRD source tree: '${nrd_source_dir}'.")
    endif()

    # Keep each local source tree's CMake cache separate from the downloaded checkout.
    string(SHA256 nrd_source_hash "${nrd_source_dir}")
    string(SUBSTRING "${nrd_source_hash}" 0 12 nrd_source_hash)
    set(nrd_source_args
        SOURCE_DIR "${nrd_source_dir}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/local-${nrd_source_hash}"
        BUILD_ALWAYS TRUE
    )
    message(STATUS "Using local NRD source: ${nrd_source_dir}")
endif()

set(nrd_sdk_dir "${CMAKE_CURRENT_BINARY_DIR}/_NRD_SDK")
set(nrd_sdk_files
    "${nrd_sdk_dir}/Include/NRD.h"
    "${nrd_sdk_dir}/Include/NRDDescs.h"
    "${nrd_sdk_dir}/Include/NRDSettings.h"
    "${nrd_sdk_dir}/Shaders/NRD.hlsli"
    "${nrd_sdk_dir}/Shaders/NRDConfig.hlsli"
    "${nrd_sdk_dir}/LICENSE.txt"
)

set(nrd_cmake_args
    -DNRD_STATIC_LIBRARY:BOOL=ON
    -DNRD_NRI:BOOL=OFF
    -DNRD_EMBEDS_DXIL_SHADERS:BOOL=ON
    -DNRD_EMBEDS_DXBC_SHADERS:BOOL=OFF
    -DNRD_EMBEDS_SPIRV_SHADERS:BOOL=OFF
    -DNRD_NORMAL_ENCODING:STRING=2
    -DNRD_ROUGHNESS_ENCODING:STRING=2
    -DSHADERMAKE_FIND_FXC:BOOL=OFF
    -DSHADERMAKE_FIND_DXC_VK:BOOL=OFF
    "-DNRD_SHADERS_PATH:PATH=<BINARY_DIR>/shaders"
    "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY:PATH=<BINARY_DIR>/artifacts"
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY:PATH=<BINARY_DIR>/artifacts"
)

get_property(nrd_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(nrd_multi_config)
    set(nrd_configs Debug Release)
else()
    string(TOUPPER "${CMAKE_BUILD_TYPE}" nrd_requested_config)
    if(nrd_requested_config STREQUAL "DEBUG")
        set(nrd_configs Debug)
    else()
        set(nrd_configs Release)
    endif()
    list(APPEND nrd_cmake_args "-DCMAKE_BUILD_TYPE:STRING=${nrd_configs}")
endif()

set(nrd_build_commands)
set(nrd_install_commands
    COMMAND "${CMAKE_COMMAND}" -E make_directory "<INSTALL_DIR>/Include" "<INSTALL_DIR>/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "<SOURCE_DIR>/Include" "<INSTALL_DIR>/Include"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "<SOURCE_DIR>/Shaders/NRD.hlsli" "<SOURCE_DIR>/Shaders/NRDConfig.hlsli" "<INSTALL_DIR>/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "<SOURCE_DIR>/LICENSE.txt" "<INSTALL_DIR>"
)

foreach(config IN LISTS nrd_configs)
    set(nrd_artifact_dir "<BINARY_DIR>/artifacts")
    if(nrd_multi_config)
        string(APPEND nrd_artifact_dir "/${config}")
    endif()

    list(APPEND nrd_build_commands
        COMMAND "${CMAKE_COMMAND}" --build "<BINARY_DIR>" --config "${config}" --target NRD)
    list(APPEND nrd_install_commands
        COMMAND "${CMAKE_COMMAND}" -E make_directory "<INSTALL_DIR>/Lib/${config}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${nrd_artifact_dir}/NRD.lib" "${nrd_artifact_dir}/ShaderMakeBlob.lib" "<INSTALL_DIR>/Lib/${config}")
    list(APPEND nrd_sdk_files
        "${nrd_sdk_dir}/Lib/${config}/NRD.lib"
        "${nrd_sdk_dir}/Lib/${config}/ShaderMakeBlob.lib")
endforeach()

# ExternalProject expects the first command without a COMMAND separator.
list(POP_FRONT nrd_build_commands)
list(POP_FRONT nrd_install_commands)

ExternalProject_Add(nrd-sdk
    ${nrd_source_args}
    INSTALL_DIR "${nrd_sdk_dir}"
    CMAKE_ARGS ${nrd_cmake_args}
    BUILD_COMMAND ${nrd_build_commands}
    INSTALL_COMMAND ${nrd_install_commands}
    INSTALL_BYPRODUCTS ${nrd_sdk_files}
)

message(STATUS "NRD SDK output: ${nrd_sdk_dir}")
