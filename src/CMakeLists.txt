# Main executable
add_executable(exchange main.cpp)

# ── gperf: compile-time perfect hash for ticker symbols ─────────────
find_program(GPERF gperf REQUIRED)

set(GPERF_INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/lib/exchange/tickers.gperf")
set(GPERF_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gen/tickers_gperf.hpp")

add_custom_command(
    OUTPUT ${GPERF_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/gen"
    COMMAND ${GPERF}
        -g
        --language=C++
        --readonly-tables
        --compare-strncmp
        --struct-type
        --class-name=TickersHash
        --output-file=${GPERF_OUTPUT}
        ${GPERF_INPUT}
    # COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/fixup_gperf.sh ${GPERF_OUTPUT}
    DEPENDS ${GPERF_INPUT}
    COMMENT "Generating perfect hash for ticker symbols (gperf)"
)

add_custom_target(generate_tickers DEPENDS ${GPERF_OUTPUT})

# Library
add_library(exchange_lib STATIC
    lib/exchange/engine.cpp
    lib/exchange/book.cpp
    lib/exchange/fix/fix_session.cpp
    lib/exchange/fix/fix_gateway.cpp
)

add_dependencies(exchange_lib generate_tickers)

target_include_directories(exchange_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/gen
)

target_link_libraries(exchange_lib PUBLIC type_safe glaze::glaze fixpp)
target_link_libraries(exchange PRIVATE exchange_lib Boost::program_options Threads::Threads)
