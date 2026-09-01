option(MaterialSystemTests "Material system tests" ON)
option(MaterialSerializeTests "Material reflection/JSON serialization tests" ON)

set(TEST_SOURCES "")

function(ADD_TEST_SOUECE TEST_OPTION TEST_SOURCE_FILE)
    if (${TEST_OPTION})
        set(TEST_SOURCES ${TEST_SOURCES} ${TEST_SOURCE_FILE} PARENT_SCOPE)
    endif()
endfunction(ADD_TEST_SOUECE)

ADD_TEST_SOUECE(MaterialSystemTests "MaterialSystemTest.cpp")
ADD_TEST_SOUECE(MaterialSerializeTests "MaterialSerializeTest.cpp")

set(TARGET_NAME SparkMaterialTest)

add_executable(${TARGET_NAME}
    ${TEST_SOURCES}
    main.cpp
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        SparkMaterial
        gtest_main
        gmock_main
)

target_compile_definitions(${TARGET_NAME}
    PRIVATE
        TEST_MATERIAL_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# 添加为CTest测试
add_test(NAME SparkMaterialTest
    COMMAND $<TARGET_FILE:${TARGET_NAME}>
)
