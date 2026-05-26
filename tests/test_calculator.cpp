#include <gtest/gtest.h>

#include "lib/calculator.h"

TEST(CalculatorTest, Add) {
    EXPECT_DOUBLE_EQ(Calculator::add(2.0, 3.0), 5.0);
    EXPECT_DOUBLE_EQ(Calculator::add(-1.0, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(Calculator::add(0.0, 0.0), 0.0);
}

TEST(CalculatorTest, Subtract) {
    EXPECT_DOUBLE_EQ(Calculator::subtract(10.0, 4.0), 6.0);
    EXPECT_DOUBLE_EQ(Calculator::subtract(0.0, 5.0), -5.0);
}

TEST(CalculatorTest, Multiply) {
    EXPECT_DOUBLE_EQ(Calculator::multiply(6.0, 7.0), 42.0);
    EXPECT_DOUBLE_EQ(Calculator::multiply(0.0, 100.0), 0.0);
}

TEST(CalculatorTest, Divide) {
    EXPECT_DOUBLE_EQ(Calculator::divide(20.0, 5.0), 4.0);
    EXPECT_DOUBLE_EQ(Calculator::divide(7.0, 2.0), 3.5);
}

TEST(CalculatorTest, DivideByZeroThrows) {
    EXPECT_THROW(static_cast<void>(Calculator::divide(10.0, 0.0)), std::invalid_argument);
}
