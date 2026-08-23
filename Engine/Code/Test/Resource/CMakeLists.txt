option(ShaderAssetTests      "Shader Asset Tests"      ON)
option(ImageAssetTests       "Image Asset Tests"       ON)
option(ModelAssetTests       "Model Asset Tests"       ON)
option(EnvironmentBakerTests "Environment Baker Tests" ON)


set(TEST_SOURCES "")

function(ADD_TEST_SOUECE TEST_OPTION TEST_SOURCE_FILE)
    if (${TEST_OPTION})
        set(TEST_SOURCES ${TEST_SOURCES} ${TEST_SOURCE_FILE} PARENT_SCOPE)
    endif()
endfunction(ADD_TEST_SOUECE)

ADD_TEST_SOUECE(ShaderAssetTests "ShaderAssetTest.cpp")
ADD_TEST_SOUECE(ImageAssetTests  "ImageAssetTests.cpp")
ADD_TEST_SOUECE(ModelAssetTests  "ModelAssetTests.cpp")
ADD_TEST_SOUECE(EnvironmentBakerTests "EnvironmentBakerTests.cpp")


set(TARGET_NAME SparkAssetTest)

Message(STATUS ${TEST_SOURCES})

add_executable(${TARGET_NAME}
    ${TEST_SOURCES}
    main.cpp
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        SparkAssetManager
        SparkRender
        gtest_main
        gmock_main
)

target_compile_definitions(${TARGET_NAME}
    PRIVATE
        ENGINE_ASSET_DIR="${ENGINE_ASSET_DIR}"
        TEST_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# 添加为CTest测试
add_test(NAME SparkAssetTest 
    COMMAND $<TARGET_FILE:${TARGET_NAME}> 
)
