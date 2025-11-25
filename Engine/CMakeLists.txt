
set(ENGINE_ROOT_DIR "${SPARK_ROOT_DIR}/Engine")
set(THIRD_PARTY_DIR "${ENGINE_ROOT_DIR}/3rdParty")
set(ENGINE_ASSET_DIR "${ENGINE_ROOT_DIR}/Asset")


option(BUILD_TESTS "Build the tests" ON)

add_compile_definitions(NOMINMAX)
add_compile_definitions(MATH_BACKEND_GLM)

add_subdirectory(3rdParty)
add_subdirectory(Code/Runtime)
add_subdirectory(Code/Editor)

# 如果启用测试，添加测试目录
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(Code/Test)
endif()
