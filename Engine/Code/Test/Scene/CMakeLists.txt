set(TARGET_NAME SparkSceneTest)

add_executable(${TARGET_NAME}
    SceneWriteTest.cpp
    main.cpp
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkCore
        SparkMaterial
        SparkScene
        gtest_main
        gmock_main
)

add_test(NAME SparkSceneTest
    COMMAND $<TARGET_FILE:${TARGET_NAME}>
)
