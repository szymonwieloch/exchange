cmake_minimum_required(VERSION 3.16)
project(Exchange VERSION 1.0.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    add_compile_options(/W4 /WX)
endif()

# Enable testing
enable_testing()

# Dependencies
include(FetchContent)

# debug_assert — lightweight assertion library (dependency of type_safe)
FetchContent_Declare(
    debug_assert
    URL https://github.com/foonathan/debug_assert/archive/refs/tags/v1.3.3.tar.gz
)
FetchContent_MakeAvailable(debug_assert)

# type_safe — zero-overhead type-safe utilities for C++
FetchContent_Declare(
    type_safe
    URL https://github.com/foonathan/type_safe/archive/refs/tags/v0.2.4.tar.gz
)
set(TYPE_SAFE_FETCH_MISSING_DEPENDENCIES OFF CACHE BOOL "")
FetchContent_MakeAvailable(type_safe)

# Subdirectories
add_subdirectory(src)

# Build tests only if not a subproject and testing is enabled
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
