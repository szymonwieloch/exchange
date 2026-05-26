#include <gtest/gtest.h>

#include <thread>

#include "lib/utils/thread.h"

TEST(ThreadTest, SetThreadCoreReturnsBool) {
    // Core 0 should exist on any Linux system. The call may fail if we
    // lack CAP_SYS_NICE, but it must not crash and must return a bool.
    [[maybe_unused]] const bool result = utils::setThreadCore(0);
    SUCCEED();  // just verify the call compiles and doesn't crash
}

TEST(ThreadTest, SetThreadCoreInvalidCore) {
    // A ridiculously high core number should fail.
    EXPECT_FALSE(utils::setThreadCore(999999));
}

TEST(ThreadTest, SetThreadCoreNegativeCore) {
    // Negative core ids are nonsensical — CPU_SET treats them as large
    // unsigned values, so they should fail (out of range).
    EXPECT_FALSE(utils::setThreadCore(-1));
}
