# Main executable
add_executable(exchange main.cpp)

# Library
add_library(exchange_lib STATIC
    lib/exchange/engine.cpp
    lib/exchange/book.cpp
    lib/exchange/fix/fix_session.cpp
    lib/exchange/fix/fix_gateway.cpp
)

target_include_directories(exchange_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exchange_lib PUBLIC type_safe glaze::glaze fixpp)
target_link_libraries(exchange PRIVATE exchange_lib Boost::program_options Threads::Threads)
