#pragma once

class Calculator {
public:
    [[nodiscard]] static double add(double lhs, double rhs);
    [[nodiscard]] static double subtract(double lhs, double rhs);
    [[nodiscard]] static double multiply(double lhs, double rhs);
    [[nodiscard]] static double divide(double lhs, double rhs);
};
