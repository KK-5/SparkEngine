
set(ENGINE_ROOT_DIR "${SPARK_ROOT_DIR}/Engine")
set(THIRD_PARTY_DIR "${ENGINE_ROOT_DIR}/3rdParty")
set(ENGINE_ASSET_DIR "${ENGINE_ROOT_DIR}/Asset")


option(BUILD_TESTS "Build the tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BINARY_ROOT_DIR}")

add_subdirectory(3rdParty)
add_subdirectory(Code/Runtime)
add_subdirectory(Code/Editor)

# 如果启用测试，添加测试目录
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(Code/Test)
endif()

# 拷贝 DXC 运行时 DLL 到输出目录
add_custom_target(copy_dxc_dlls ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${THIRD_PARTY_DIR}/DXC/bin/x64/dxcompiler.dll"
        "${THIRD_PARTY_DIR}/DXC/bin/x64/dxil.dll"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
    COMMENT "Copying DXC DLLs"
)
