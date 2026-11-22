option(SPARK_WITH_NRD "Build with NVIDIA Real-Time Denoisers" ON)
set(SPARK_NRD_SDK_DIR "" CACHE PATH "Path to a built Spark NRD SDK (empty uses build/nrd-sdk/_NRD_SDK)")
if(NOT SPARK_WITH_NRD)
    return()
endif()

if(SPARK_NRD_SDK_DIR STREQUAL "")
    set(nrd_sdk_dir "${SPARK_ROOT_DIR}/build/nrd-sdk/_NRD_SDK")
else()
    get_filename_component(nrd_sdk_dir "${SPARK_NRD_SDK_DIR}" ABSOLUTE BASE_DIR "${SPARK_ROOT_DIR}")
endif()

foreach(nrd_file IN ITEMS
        Include/NRD.h Include/NRDDescs.h Include/NRDSettings.h
        Shaders/NRD.hlsli Shaders/NRDConfig.hlsli)
    if(NOT EXISTS "${nrd_sdk_dir}/${nrd_file}")
        message(FATAL_ERROR "NRD SDK is missing '${nrd_file}' in '${nrd_sdk_dir}'. Build the SDK from cmake/NRD, "
            "set SPARK_NRD_SDK_DIR to an existing SDK, or configure with -DSPARK_WITH_NRD=OFF.")
    endif()
endforeach()

add_library(NRD::NRD STATIC IMPORTED GLOBAL)
add_library(NRD::ShaderMakeBlob STATIC IMPORTED GLOBAL)
set_target_properties(NRD::NRD PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${nrd_sdk_dir}/Include"
    INTERFACE_COMPILE_DEFINITIONS NRD_STATIC_LIBRARY=1
    INTERFACE_LINK_LIBRARIES NRD::ShaderMakeBlob
    SPARK_NRD_SHADER_DIR "${nrd_sdk_dir}/Shaders"
)

if(CMAKE_CONFIGURATION_TYPES)
    set(nrd_consumer_configs ${CMAKE_CONFIGURATION_TYPES})
elseif(CMAKE_BUILD_TYPE)
    set(nrd_consumer_configs "${CMAKE_BUILD_TYPE}")
else()
    set(nrd_consumer_configs NOCONFIG)
endif()

foreach(nrd_config IN LISTS nrd_consumer_configs)
    string(TOUPPER "${nrd_config}" nrd_config_upper)
    if(nrd_config_upper STREQUAL "DEBUG")
        set(nrd_sdk_config Debug)
    else()
        set(nrd_sdk_config Release)
    endif()

    foreach(nrd_library IN ITEMS NRD ShaderMakeBlob)
        set(nrd_library_path "${nrd_sdk_dir}/Lib/${nrd_sdk_config}/${nrd_library}.lib")
        if(NOT EXISTS "${nrd_library_path}")
            message(FATAL_ERROR "Missing NRD SDK library '${nrd_library_path}' for Spark configuration '${nrd_config}'. Build the ${nrd_sdk_config} SDK from cmake/NRD.")
        endif()

        set_property(TARGET NRD::${nrd_library} APPEND PROPERTY IMPORTED_CONFIGURATIONS "${nrd_config_upper}")
        set_property(TARGET NRD::${nrd_library} PROPERTY IMPORTED_LOCATION_${nrd_config_upper} "${nrd_library_path}")
    endforeach()
endforeach()

message(STATUS "Using NRD SDK: ${nrd_sdk_dir}")
