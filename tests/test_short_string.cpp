#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>

#include "lib/utils/short_string.h"

using namespace utils;

// ===================================================================
//  Note: static_assert compile-time tests are omitted because GCC 13 does not
//  treat __builtin_memcpy as constexpr.  The constexpr annotations on all
//  ShortString functions will take effect when building with GCC 14+ / Clang 17+.
//  All functionality is covered by runtime tests below.
// ===================================================================

// ===================================================================
//  Default construction
// ===================================================================

TEST(ShortStringTest, DefaultConstructedIsEmpty) {
    ShortString s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_EQ(s.view(), "");
}

TEST(ShortStringTest, DefaultConstructedDataIsNullTerminated) {
    ShortString s;
    EXPECT_EQ(s.data()[0], '\0');
}

// ===================================================================
//  shorten(std::string_view)
// ===================================================================

TEST(ShortStringTest, ShortenStringViewEmpty) {
    auto s = ShortString::shorten(std::string_view{""});
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_EQ(s.view(), "");
}

TEST(ShortStringTest, ShortenStringViewNormal) {
    auto s = ShortString::shorten(std::string_view{"AAPL"});
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.size(), 4u);
    EXPECT_EQ(s.view(), "AAPL");
    EXPECT_STREQ(s.data(), "AAPL");
}

TEST(ShortStringTest, ShortenStringViewSingleChar) {
    auto s = ShortString::shorten(std::string_view{"X"});
    EXPECT_EQ(s.size(), 1u);
    EXPECT_EQ(s.view(), "X");
}

TEST(ShortStringTest, ShortenStringViewExactly8Chars) {
    auto s = ShortString::shorten(std::string_view{"12345678"});
    EXPECT_EQ(s.size(), 8u);
    EXPECT_EQ(s.view(), "12345678");
    // data()[7] is non-null (no room for null terminator)
    EXPECT_NE(s.data()[7], '\0');
}

TEST(ShortStringTest, ShortenStringViewTruncatesBeyond8) {
    auto s = ShortString::shorten(std::string_view{"123456789ABCDEF"});
    EXPECT_EQ(s.size(), 8u);
    EXPECT_EQ(s.view(), "12345678");
}

// ===================================================================
//  shorten(const std::string&)
// ===================================================================

TEST(ShortStringTest, ShortenStdStringEmpty) {
    auto s = ShortString::shorten(std::string{""});
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(ShortStringTest, ShortenStdStringNormal) {
    auto s = ShortString::shorten(std::string{"GOOGL"});
    EXPECT_EQ(s.size(), 5u);
    EXPECT_EQ(s.view(), "GOOGL");
}

TEST(ShortStringTest, ShortenStdStringTruncatesBeyond8) {
    auto s = ShortString::shorten(std::string{"longstring12345"});
    EXPECT_EQ(s.size(), 8u);
    EXPECT_EQ(s.view(), "longstri");
}

// ===================================================================
//  shorten(const char*)
// ===================================================================

TEST(ShortStringTest, ShortenCstrEmpty) {
    auto s = ShortString::shorten("");
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(ShortStringTest, ShortenCstrNormal) {
    auto s = ShortString::shorten("MSFT");
    EXPECT_EQ(s.size(), 4u);
    EXPECT_EQ(s.view(), "MSFT");
}

TEST(ShortStringTest, ShortenCstrExactly8Chars) {
    auto s = ShortString::shorten("12345678");
    EXPECT_EQ(s.size(), 8u);
    EXPECT_EQ(s.view(), "12345678");
}

TEST(ShortStringTest, ShortenCstrLongStringTruncatesTo8) {
    auto s = ShortString::shorten("123456789ABCDEFGHIJ");
    EXPECT_EQ(s.size(), 8u);
    EXPECT_EQ(s.view(), "12345678");
}

TEST(ShortStringTest, ShortenCstrWithEmbeddedNull) {
    // "AB\0CD" — should stop at the null, producing "AB"
    char buf[8] = {'A', 'B', '\0', 'C', 'D', '\0', '\0', '\0'};
    auto s = ShortString::shorten(static_cast<const char *>(buf));
    EXPECT_EQ(s.size(), 2u);
    EXPECT_EQ(s.view(), "AB");
}

TEST(ShortStringTest, ShortenCstrNullByteAtFirstPosition) {
    char buf[8] = {'\0', 'X', 'Y', 'Z', '\0', '\0', '\0', '\0'};
    auto s = ShortString::shorten(static_cast<const char *>(buf));
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(ShortStringTest, ShortenCstrAllThreeOverloadsProduceSameResult) {
    const char *cstr = "trade42";
    std::string str = "trade42";
    std::string_view sv = "trade42";

    auto s1 = ShortString::shorten(cstr);
    auto s2 = ShortString::shorten(str);
    auto s3 = ShortString::shorten(sv);

    EXPECT_EQ(s1, s2);
    EXPECT_EQ(s2, s3);
    EXPECT_EQ(s1.view(), "trade42");
    EXPECT_EQ(s2.view(), "trade42");
    EXPECT_EQ(s3.view(), "trade42");
}

// ===================================================================
//  Copy / move constructors
// ===================================================================

TEST(ShortStringTest, CopyConstructorProducesEqualValue) {
    auto orig = ShortString::shorten("ABCD");
    ShortString copy(orig);
    EXPECT_EQ(copy, orig);
    EXPECT_EQ(copy.size(), 4u);
    EXPECT_EQ(copy.view(), "ABCD");
}

TEST(ShortStringTest, CopyConstructorIsIndependent) {
    auto orig = ShortString::shorten("ABCD");
    ShortString copy(orig);
    // Modifying the original (via reassignment) does not affect the copy
    orig = ShortString::shorten("WXYZ");
    EXPECT_EQ(copy.view(), "ABCD");
    EXPECT_EQ(orig.view(), "WXYZ");
}

TEST(ShortStringTest, MoveConstructorPreservesValue) {
    auto orig = ShortString::shorten("MOVE");
    ShortString moved(std::move(orig));
    EXPECT_EQ(moved.size(), 4u);
    EXPECT_EQ(moved.view(), "MOVE");
    // orig is still valid (trivially copyable), just in a moved-from state.
    // For a trivially copyable type, the value is unchanged.
    EXPECT_EQ(orig.view(), "MOVE");
}

// ===================================================================
//  Copy / move assignment
// ===================================================================

TEST(ShortStringTest, CopyAssignmentProducesEqualValue) {
    auto orig = ShortString::shorten("CPAS");
    ShortString assigned;
    assigned = orig;
    EXPECT_EQ(assigned, orig);
    EXPECT_EQ(assigned.view(), "CPAS");
}

TEST(ShortStringTest, CopyAssignmentIsIndependent) {
    auto orig = ShortString::shorten("BEFORE");
    ShortString assigned = ShortString::shorten("AFTER");
    assigned = orig;
    orig = ShortString::shorten("CHANGED");
    EXPECT_EQ(assigned.view(), "BEFORE");
    EXPECT_EQ(orig.view(), "CHANGED");
}

TEST(ShortStringTest, MoveAssignmentPreservesValue) {
    auto orig = ShortString::shorten("MVAS");
    ShortString target = ShortString::shorten("OLD");
    target = std::move(orig);
    EXPECT_EQ(target.view(), "MVAS");
}

TEST(ShortStringTest, SelfCopyAssignmentIsSafe) {
    auto s = ShortString::shorten("SELF");
    s = s;  // self copy-assign
    EXPECT_EQ(s.view(), "SELF");
}

TEST(ShortStringTest, SelfMoveAssignmentIsSafe) {
    auto s = ShortString::shorten("SELF");
    // Suppress -Wself-move: self-move of trivially-copyable type is a no-op.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
    s = std::move(s);
#pragma GCC diagnostic pop
    EXPECT_EQ(s.view(), "SELF");
}

// ===================================================================
//  data(), size(), empty(), view()
// ===================================================================

TEST(ShortStringTest, DataReturnsNullTerminatedBuffer) {
    auto s = ShortString::shorten("OK");
    EXPECT_EQ(s.data()[0], 'O');
    EXPECT_EQ(s.data()[1], 'K');
    EXPECT_EQ(s.data()[2], '\0');
}

TEST(ShortStringTest, DataOnEmpty) {
    ShortString s;
    EXPECT_EQ(s.data()[0], '\0');
}

TEST(ShortStringTest, SizeReflectsLength) {
    EXPECT_EQ(ShortString::shorten("").size(), 0u);
    EXPECT_EQ(ShortString::shorten("a").size(), 1u);
    EXPECT_EQ(ShortString::shorten("ab").size(), 2u);
    EXPECT_EQ(ShortString::shorten("abcdefgh").size(), 8u);
}

TEST(ShortStringTest, EmptyIsTrueOnlyWhenSizeIsZero) {
    EXPECT_TRUE(ShortString().empty());
    EXPECT_TRUE(ShortString::shorten("").empty());
    EXPECT_FALSE(ShortString::shorten("a").empty());
    EXPECT_FALSE(ShortString::shorten("12345678").empty());
}

TEST(ShortStringTest, ViewReturnsCorrectStringView) {
    auto s = ShortString::shorten("viewtest");
    std::string_view sv = s.view();
    EXPECT_EQ(sv, "viewtest");
    EXPECT_EQ(sv.size(), 8u);  // truncated to 8
}

// ===================================================================
//  operator== / operator!=
// ===================================================================

TEST(ShortStringTest, EqualWhenSameContent) {
    auto a = ShortString::shorten("SAME");
    auto b = ShortString::shorten("SAME");
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST(ShortStringTest, NotEqualWhenDifferentContent) {
    auto a = ShortString::shorten("LEFT");
    auto b = ShortString::shorten("RIGHT");
    EXPECT_NE(a, b);
    EXPECT_FALSE(a == b);
}

TEST(ShortStringTest, NotEqualWhenDifferentLengths) {
    auto a = ShortString::shorten("A");
    auto b = ShortString::shorten("AB");
    EXPECT_NE(a, b);
}

TEST(ShortStringTest, EqualityAgainstEmpty) {
    auto empty1 = ShortString();
    auto empty2 = ShortString::shorten("");
    auto nonempty = ShortString::shorten("X");
    EXPECT_EQ(empty1, empty2);
    EXPECT_NE(empty1, nonempty);
}

TEST(ShortStringTest, EqualityIsSymmetric) {
    auto a = ShortString::shorten("SYM");
    auto b = ShortString::shorten("SYM");
    EXPECT_EQ(a, b);
    EXPECT_EQ(b, a);
}

// ===================================================================
//  operator<<
// ===================================================================

TEST(ShortStringTest, OstreamOutput) {
    auto s = ShortString::shorten("hello");
    std::ostringstream oss;
    oss << s;
    EXPECT_EQ(oss.str(), "hello");
}

TEST(ShortStringTest, OstreamOutputEmpty) {
    ShortString s;
    std::ostringstream oss;
    oss << s;
    EXPECT_EQ(oss.str(), "");
}

TEST(ShortStringTest, OstreamOutputTruncated) {
    auto s = ShortString::shorten("1234567890");
    std::ostringstream oss;
    oss << s;
    EXPECT_EQ(oss.str(), "12345678");
}

// ===================================================================
//  Edge cases
// ===================================================================

TEST(ShortStringTest, ExactBoundary7Chars) {
    auto s = ShortString::shorten("1234567");
    EXPECT_EQ(s.size(), 7u);
    EXPECT_EQ(s.data()[6], '7');
    EXPECT_EQ(s.data()[7], '\0');  // null terminator fits
}

TEST(ShortStringTest, ExactBoundary8CharsNoNullRoom) {
    auto s = ShortString::shorten("12345678");
    EXPECT_EQ(s.size(), 8u);
    // data()[7] is '8', not '\0'
    EXPECT_EQ(s.data()[7], '8');
}

TEST(ShortStringTest, ShortenCstrSingleChar) {
    auto s = ShortString::shorten("Z");
    EXPECT_EQ(s.size(), 1u);
    EXPECT_EQ(s.view(), "Z");
    EXPECT_EQ(s.data()[0], 'Z');
    EXPECT_EQ(s.data()[1], '\0');
}

TEST(ShortStringTest, ShortenCstrTwoChars) {
    auto s = ShortString::shorten("XY");
    EXPECT_EQ(s.size(), 2u);
    EXPECT_EQ(s.view(), "XY");
}
