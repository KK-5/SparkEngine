option(SHADOWATLAS_TESTS "Shadow atlas allocator tests" ON)
option(MATERIALOVERRIDE_TESTS "Per-object material override synthesis tests" ON)
option(HANDLEPOOL_TESTS "Reference-counted id pool tests" ON)
option(SLOTPOOL_TESTS "Stable-slot pool tests" ON)
option(IMAGESUBRESOURCESTATES_TESTS "Per-subresource image state tests" ON)
option(IMAGECOPYALIGNMENT_TESTS "Image copy offset alignment tests" ON)
option(IMAGEBARRIERMERGE_TESTS "Per-Scope image barrier merge tests" ON)

set(TEST_SOURCES "")

function(ADD_TEST_SOUECE TEST_OPTION TEST_SOURCE_FILE)
    if (${TEST_OPTION})
        set(TEST_SOURCES ${TEST_SOURCES} ${TEST_SOURCE_FILE} PARENT_SCOPE)
    endif()
endfunction(ADD_TEST_SOUECE)

ADD_TEST_SOUECE(SHADOWATLAS_TESTS "ShadowAtlasAllocatorTest.cpp")
ADD_TEST_SOUECE(MATERIALOVERRIDE_TESTS "MaterialOverrideTest.cpp")
ADD_TEST_SOUECE(HANDLEPOOL_TESTS "HandlePoolTest.cpp")
ADD_TEST_SOUECE(SLOTPOOL_TESTS "SlotPoolTest.cpp")
ADD_TEST_SOUECE(IMAGESUBRESOURCESTATES_TESTS "ImageSubresourceStatesTest.cpp")
ADD_TEST_SOUECE(IMAGECOPYALIGNMENT_TESTS "ImageCopyAlignmentTest.cpp")
ADD_TEST_SOUECE(IMAGEBARRIERMERGE_TESTS "ImageBarrierMergeTest.cpp")

set(TARGET_NAME SparkRenderTest)

add_executable(${TARGET_NAME}
    ${TEST_SOURCES}
    main.cpp
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        SparkRender
        SparkMaterial   # SparkRender keeps SparkAssetManager private; Resource/ headers come through here
        gtest_main
        gmock_main
)

# 添加为CTest测试
add_test(NAME SparkRenderTest
    COMMAND $<TARGET_FILE:${TARGET_NAME}>
)
