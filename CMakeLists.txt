cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

project(SparkEngine VERSION 0.1.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CMakeDependentOption)

set(SPARK_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_INSTALL_PREFIX "${SPARK_ROOT_DIR}/bin")
set(BINARY_ROOT_DIR "${CMAKE_INSTALL_PREFIX}/")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BINARY_ROOT_DIR}")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /Zi")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG /OPT:REF /OPT:ICF")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DEBUG /OPT:REF /OPT:ICF")
endif()

add_compile_definitions(NOMINMAX)
add_compile_definitions(MATH_BACKEND_GLM)

if(MSVC)
    # /Ob0 emits every inline function as its own COMDAT, and EASTL/entt are almost
    # nothing but inline functions — a template-heavy TU runs out of the 65,279 COFF
    # sections an object file can hold. Expanding them instead cuts the section count
    # by more than half, and makes Debug several times faster. The cost is that those
    # functions can no longer be stepped into; turn this off when that is what you need.
    option(SPARK_DEBUG_INLINE "Expand inline functions in Debug builds" ON)
    if(SPARK_DEBUG_INLINE)
        # REPLACE, not append: "/Ob0 /Ob1" works but warns D9025 once per file.
        string(REPLACE "/Ob0" "/Ob1" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    endif()
endif()

# Must be here, not only in Engine/: enable_testing() generates CTestTestfile.cmake for
# the calling directory downwards, so calling it solely in Engine/ leaves the build root
# without one and `ctest --test-dir build` reports "No tests were found".
enable_testing()

add_subdirectory(Engine)
add_subdirectory(SandBox)