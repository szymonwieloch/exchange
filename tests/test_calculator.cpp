#include <gtest/gtest.h>

#include "lib/calculator.h"

TEST(CalculatorTest, Add) {
    Calculator calc;
    EXPECT_DOUBLE_EQ(calc.add(2.0, 3.0), 5.0);
    EXPECT_DOUBLE_EQ(calc.add(-1.0, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(calc.add(0.0, 0.0), 0.0);
}

TEST(CalculatorTest, Subtract) {
    Calculator calc;
    EXPECT_DOUBLE_EQ(calc.subtract(10.0, 4.0), 6.0);
    EXPECT_DOUBLE_EQ(calc.subtract(0.0, 5.0), -5.0);
}

TEST(CalculatorTest, Multiply) {
    Calculator calc;
    EXPECT_DOUBLE_EQ(calc.multiply(6.0, 7.0), 42.0);
    EXPECT_DOUBLE_EQ(calc.multiply(0.0, 100.0), 0.0);
}

TEST(CalculatorTest, Divide) {
    Calculator calc;
    EXPECT_DOUBLE_EQ(calc.divide(20.0, 5.0), 4.0);
    EXPECT_DOUBLE_EQ(calc.divide(7.0, 2.0), 3.5);
}

TEST(CalculatorTest, DivideByZeroThrows) {
    Calculator calc;
    EXPECT_THROW(calc.divide(10.0, 0.0), std::invalid_argument);
}
