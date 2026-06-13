# Google Test
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
)

# Prevent gtest from installing anything
set(INSTALL_GTEST OFF CACHE BOOL "")

FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(exchange_tests
    test_book.cpp
    test_mempool.cpp
    test_orders_at_price.cpp
    test_queue.cpp
    test_thread.cpp
    test_time.cpp
    test_user_orders.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/exchange/engine.cpp
    ${PROJECT_SOURCE_DIR}/src/lib/exchange/book.cpp
)

target_include_directories(exchange_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(exchange_tests PRIVATE
    GTest::gtest_main
    type_safe
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(exchange_tests)
