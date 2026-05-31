#pragma once

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace utils {
[[noreturn]] inline void die(std::string_view message, int exit_code = 1) {
    std::cerr << message << std::endl;
    std::exit(exit_code);
}
}  // namespace utils