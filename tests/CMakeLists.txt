# Google Test
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)

# Prevent gtest from installing anything
set(INSTALL_GTEST OFF CACHE BOOL "")

FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(exchange_tests
    test_calculator.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/calculator.cpp
)

target_include_directories(exchange_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(exchange_tests PRIVATE
    GTest::gtest_main
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(exchange_tests)
