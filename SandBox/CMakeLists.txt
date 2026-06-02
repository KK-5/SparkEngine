
set(SANDBOX_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Asset")
set(ENGINE_ROOT_DIR "${SPARK_ROOT_DIR}/Engine")
set(ENGINE_SHADER_DIR "${ENGINE_ROOT_DIR}/Asset/Shaders")
set(THIRD_PARTY_DIR "${ENGINE_ROOT_DIR}/3rdParty")

option(BUILD_RHI_HELLOTRIANGLE "Build RHI HelloTriangle" ON)

add_subdirectory(Program)
