#include <iostream>
#include "lib/calculator.h"

int main() {
    Calculator calc;
    std::cout << "2 + 3 = " << calc.add(2, 3) << '\n';
    std::cout << "10 - 4 = " << calc.subtract(10, 4) << '\n';
    std::cout << "6 * 7 = " << calc.multiply(6, 7) << '\n';
    std::cout << "20 / 5 = " << calc.divide(20, 5) << '\n';
    return 0;
}
