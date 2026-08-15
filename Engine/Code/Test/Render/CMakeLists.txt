option(SHADOWATLAS_TESTS "Shadow atlas allocator tests" ON)

set(TEST_SOURCES "")

function(ADD_TEST_SOUECE TEST_OPTION TEST_SOURCE_FILE)
    if (${TEST_OPTION})
        set(TEST_SOURCES ${TEST_SOURCES} ${TEST_SOURCE_FILE} PARENT_SCOPE)
    endif()
endfunction(ADD_TEST_SOUECE)

ADD_TEST_SOUECE(SHADOWATLAS_TESTS "ShadowAtlasAllocatorTest.cpp")

set(TARGET_NAME SparkRenderTest)

Message(STATUS ${TEST_SOURCES})

add_executable(${TARGET_NAME}
    ${TEST_SOURCES}
    main.cpp
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        SparkRender
        gtest_main
        gmock_main
)

# 添加为CTest测试
add_test(NAME SparkRenderTest
    COMMAND $<TARGET_FILE:${TARGET_NAME}>
)
