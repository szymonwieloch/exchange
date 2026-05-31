# Main executable
add_executable(exchange main.cpp)

# Library
add_library(exchange_lib STATIC
    lib/book/engine.cpp
    lib/book/book.cpp
)

target_include_directories(exchange_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(exchange PRIVATE exchange_lib)
