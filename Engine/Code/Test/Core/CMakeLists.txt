option(Reflection_TESTS "Reflection tests" ON)
option(ECS_TESTS "ECS tests" ON)
option(Log_TESTS "Log tests" OFF)
option(EASTL_TESTS "EASTL tests" OFF)
option(EBus_TESTS "EBus tests" OFF)
option(SCENEMANAGER_TESTS "Scene manager tests" ON)
option(EXECUTECONTEXT_TESTS "ExecuteContext tests" ON)
option(WORLDCONTEXTREFERENCE_TESTS "WorldContextReference tests" ON)
option(MATH_TESTS "Math tests" ON)
option(QUADTREEALLOCATOR_TESTS "QuadTreeAllocator tests" ON)
option(VFS_TESTS "VFS mount table tests" ON)
option(JSONSERIALIZER_TESTS "Reflection-driven JSON serializer tests" ON)

set(TEST_SOURCES "")

function(ADD_TEST_SOUECE TEST_OPTION TEST_SOURCE_FILE)
    if (${TEST_OPTION})
        set(TEST_SOURCES ${TEST_SOURCES} ${TEST_SOURCE_FILE} PARENT_SCOPE)
    endif()
endfunction(ADD_TEST_SOUECE)

ADD_TEST_SOUECE(Reflection_TESTS "ReflectionTest.cpp")
ADD_TEST_SOUECE(ECS_TESTS "ECSTest.cpp")
ADD_TEST_SOUECE(Log_TESTS "LogTest.cpp")
ADD_TEST_SOUECE(EASTL_TESTS "EASTLTest.cpp")
ADD_TEST_SOUECE(EBus_TESTS "EBusTest.cpp")
ADD_TEST_SOUECE(SCENEMANAGER_TESTS "SceneManagerTest.cpp")
ADD_TEST_SOUECE(EXECUTECONTEXT_TESTS "ExecuteContextTest.cpp")
ADD_TEST_SOUECE(WORLDCONTEXTREFERENCE_TESTS "WorldContextReferenceTest.cpp")
ADD_TEST_SOUECE(MATH_TESTS "MathTest.cpp")
ADD_TEST_SOUECE(QUADTREEALLOCATOR_TESTS "QuadTreeAllocatorTest.cpp")
ADD_TEST_SOUECE(VFS_TESTS "VFSTest.cpp")
ADD_TEST_SOUECE(JSONSERIALIZER_TESTS "JsonSerializerTest.cpp")

set(TARGET_NAME SparkCoreTest)

Message(STATUS "These source files will be add to tests.")
Message(STATUS ${TEST_SOURCES})

add_executable(${TARGET_NAME}
    ${TEST_SOURCES}
    test_main.cpp
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        gtest_main
        gmock_main
        glm
)

# 添加为CTest测试
add_test(NAME SparkCoreTest 
    COMMAND $<TARGET_FILE:${TARGET_NAME}> 
)

