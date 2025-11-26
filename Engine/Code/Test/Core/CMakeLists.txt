option(Reflection_TESTS "Reflection tests" ON)
option(ECS_TESTS "ECS tests" OFF)
option(Log_TESTS "Log tests" OFF)
option(EASTL_TESTS "EASTL tests" OFF)
option(EBus_TESTS "EBus tests" OFF)
option(SCENEMANAGER_TESTS "Scene manager tests" OFF)

set(TEST_SOURCES "")

function(ADD_TSET_SOUECE TEST_OPTION TEST_SOURCE_FILE)
    if (${TEST_OPTION})
        set(TEST_SOURCES ${TEST_SOURCES} ${TEST_SOURCE_FILE} PARENT_SCOPE)
    endif()
endfunction(ADD_TSET_SOUECE)

ADD_TSET_SOUECE(Reflection_TESTS "ReflectionTest.cpp")
ADD_TSET_SOUECE(ECS_TESTS "ECSTest.cpp")
ADD_TSET_SOUECE(Log_TESTS "LogTest.cpp")
ADD_TSET_SOUECE(EASTL_TESTS "EASTLTest.cpp")
ADD_TSET_SOUECE(EBus_TESTS "EBusTest.cpp")
ADD_TSET_SOUECE(SCENEMANAGER_TESTS "SceneManagerTest.cpp")

set(TARGET_NAME SparkCoreTest)

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

