#include <iostream>

#include "lib/calculator.h"
#include "lib/utils/mem.h"
#include "lib/utils/queue.h"
#include "lib/utils/thread.h"

int main() {
    std::cout << "2 + 3 = " << Calculator::add(2, 3) << '\n';
    std::cout << "10 - 4 = " << Calculator::subtract(10, 4) << '\n';
    std::cout << "6 * 7 = " << Calculator::multiply(6, 7) << '\n';
    std::cout << "20 / 5 = " << Calculator::divide(20, 5) << '\n';
    return 0;
}
