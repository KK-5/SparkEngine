set(THIRD_PARTY_FOLDER "3rdParty")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(NOT TARGET EnTT)
    Message(STATUS "Import entt")
    add_subdirectory(entt)
    set_target_properties(EnTT PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/entt)
endif()

if(NOT TARGET spdlog)
    Message(STATUS "Import spdlog")
    add_subdirectory(spdlog)
    set_target_properties(spdlog PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/spdlog)
endif()

if(NOT TARGET EASTL)
    Message(STATUS "Import eastl")
    add_subdirectory(EASTL)
    set_target_properties(EASTL PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/EASTL)
endif()

if(NOT TARGET glfw)
    Message(STATUS "Import glfw")
    add_subdirectory(glfw)
    set_target_properties(EASTL PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/glfw)
endif()

if(NOT TARGET ImGui)
    Message(STATUS "Import imgui")
    add_subdirectory(imgui)
    set_target_properties(EASTL PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/imgui)
endif()

if(NOT TARGET glm)
    Message(STATUS "Import glm")
    add_subdirectory(glm)
    set_target_properties(glm PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/glm)
endif()

if(NOT TARGET dxc)
    Message(STATUS "Import DXC")
    add_library(dxc SHARED IMPORTED GLOBAL)
    set_target_properties(dxc PROPERTIES
        IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/DXC/lib/x64/dxcompiler.lib"
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/DXC/bin/x64/dxcompiler.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/DXC/inc"
    )
endif()

if(BUILD_TESTS)
    Message(STATUS "Import gtest")
    option(BUILD_GMOCK "Builds the googlemock subproject" ON)
    add_subdirectory(googletest)
    set_target_properties(gtest PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/googletest)
endif()