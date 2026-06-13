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

# Code coverage option
option(CODE_COVERAGE "Enable code coverage instrumentation" OFF)

if(CODE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Code coverage enabled (--coverage)")
        add_compile_options(--coverage)
        add_link_options(--coverage)
    else()
        message(FATAL_ERROR "CODE_COVERAGE requires GCC or Clang")
    endif()

    # Find required tools
    find_program(LCOV_PATH lcov REQUIRED)
    find_program(GENHTML_PATH genhtml REQUIRED)

    # Coverage report target
    set(COVERAGE_DIR "${CMAKE_BINARY_DIR}/coverage")
    set(COVERAGE_INFO "${COVERAGE_DIR}/coverage.info")

    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_DIR}
        # Reset counters
        COMMAND ${LCOV_PATH} --rc branch_coverage=1
                --directory ${CMAKE_BINARY_DIR}
                --zerocounters
        # Run tests
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
        # Capture coverage data from build dir (ignore gcov version mismatches)
        COMMAND ${LCOV_PATH} --rc branch_coverage=1
                --directory ${CMAKE_BINARY_DIR}
                --capture
                --output-file ${COVERAGE_INFO}
                --ignore-errors mismatch,empty,source
        # Remove test files, external deps, and system headers
        COMMAND ${LCOV_PATH} --rc branch_coverage=1
                --remove ${COVERAGE_INFO}
                "${PROJECT_SOURCE_DIR}/tests/*"
                "${PROJECT_SOURCE_DIR}/build/*"
                "/usr/*"
                --output-file ${COVERAGE_INFO}
                --ignore-errors unused
        # Generate HTML report
        COMMAND ${GENHTML_PATH} --rc branch_coverage=1
                ${COVERAGE_INFO}
                --output-directory ${COVERAGE_DIR}/html
                --title "Exchange Coverage Report"
                --legend
                --show-details
                --ignore-errors mismatch,empty,source
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating code coverage report..."
        VERBATIM
    )
endif()

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

# toml++ — header-only TOML configuration parser
FetchContent_Declare(
    tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
)
FetchContent_MakeAvailable(tomlplusplus)

# Boost.ProgramOptions for command-line argument parsing
find_package(Boost REQUIRED COMPONENTS program_options)

# POSIX threads (required for std::thread, metrics server, etc.)
find_package(Threads REQUIRED)

# Subdirectories
add_subdirectory(src)

# Build tests only if not a subproject and testing is enabled
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
