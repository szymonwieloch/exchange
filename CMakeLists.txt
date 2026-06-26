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

# Clang + libstdc++ compatibility: Clang reports __cpp_concepts=201907L
# but libstdc++'s <expected> header guards std::expected behind
# __cpp_concepts >= 202002L.  Force the macro so <expected> works.
# Also suppress warnings that Clang emits but GCC does not:
#   - deprecated-declarations: Boost.Beast uses std::aligned_storage
#   - braced-scalar-init: gperf-generated code uses brace init for scalars
#   - unused-private-field: fields reserved for future use
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wno-builtin-macro-redefined -Wno-deprecated-declarations
        -Wno-braced-scalar-init -Wno-unused-private-field
        -U__cpp_concepts -D__cpp_concepts=202002L)
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

# glaze — header-only C++ serialization library with TOML support
FetchContent_Declare(
    glaze
    URL https://github.com/stephenberry/glaze/archive/refs/tags/v7.8.1.tar.gz
)
FetchContent_MakeAvailable(glaze)

# fixpp — header-only type-safe FIX protocol library (C++14/20)
FetchContent_Declare(
    fixpp
    GIT_REPOSITORY https://github.com/Abc-Arbitrage/fixpp.git
    GIT_TAG d99ff616d7928b5c308adac1ef6fc3ebe34d7c1e
)

# Don't use FetchContent_MakeAvailable — fixpp's CMakeLists.txt is designed
# as a standalone project and would pull in GTest / modify global flags.
FetchContent_GetProperties(fixpp)
if(NOT fixpp_POPULATED)
    FetchContent_Populate(fixpp)
endif()

# fixpp is header-only; create an INTERFACE target for clean dependency management.
add_library(fixpp INTERFACE)
target_include_directories(fixpp SYSTEM INTERFACE
    ${fixpp_SOURCE_DIR}/include
)

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
