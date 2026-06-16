#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

#include "lib/utils/buffer.h"

using utils::Buffer;

// ===================================================================
//  Construction & initial state
// ===================================================================

TEST(BufferTest, ConstructDoesNotThrow) {
    EXPECT_NO_THROW(Buffer<64>());
}

TEST(BufferTest, InitiallyEmpty) {
    Buffer<64> buf;
    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.writeSize(), 64);
    EXPECT_EQ(buf.capacity(), 64);
}

TEST(BufferTest, DataIsNonNullEvenWhenEmpty) {
    Buffer<64> buf;
    EXPECT_NE(buf.data(), nullptr);
    EXPECT_NE(buf.writeBuf(), nullptr);
}

TEST(BufferTest, ViewOfEmptyBufferIsEmpty) {
    Buffer<64> buf;
    EXPECT_TRUE(buf.view().empty());
    EXPECT_EQ(buf.view().size(), 0);
}

// ===================================================================
//  Write & read (basic flow)
// ===================================================================

TEST(BufferTest, WriteAndReadSingleByte) {
    Buffer<16> buf;
    ASSERT_GE(buf.writeSize(), 1);

    buf.writeBuf()[0] = 'X';
    buf.extend(1);

    EXPECT_EQ(buf.size(), 1);
    EXPECT_EQ(buf.writeSize(), 15);
    EXPECT_EQ(buf.data()[0], 'X');
    EXPECT_EQ(buf.view(), std::string_view("X"));
}

TEST(BufferTest, WriteAndReadMultipleBytes) {
    Buffer<16> buf;
    const char* msg = "hello";
    std::size_t len = std::strlen(msg);

    ASSERT_GE(buf.writeSize(), len);
    std::memcpy(buf.writeBuf(), msg, len);
    buf.extend(len);

    EXPECT_EQ(buf.size(), len);
    EXPECT_EQ(buf.view(), std::string_view("hello"));
}

TEST(BufferTest, WriteUpToCapacity) {
    Buffer<8> buf;

    // Fill completely
    std::memset(buf.writeBuf(), 'A', buf.writeSize());
    buf.extend(buf.writeSize());

    EXPECT_EQ(buf.size(), 8);
    EXPECT_EQ(buf.writeSize(), 0);
    EXPECT_EQ(buf.view(), std::string_view("AAAAAAAA"));
}

// ===================================================================
//  consume()
// ===================================================================

TEST(BufferTest, ConsumePartial) {
    Buffer<16> buf;

    std::memcpy(buf.writeBuf(), "abcdef", 6);
    buf.extend(6);

    buf.consume(2);

    EXPECT_EQ(buf.size(), 4);
    EXPECT_EQ(buf.data()[0], 'c');
    EXPECT_EQ(buf.view(), std::string_view("cdef"));
}

TEST(BufferTest, ConsumeAll) {
    Buffer<16> buf;

    std::memcpy(buf.writeBuf(), "test", 4);
    buf.extend(4);
    buf.consume(4);

    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.writeSize(), 16);
    EXPECT_TRUE(buf.view().empty());
}

TEST(BufferTest, ConsumeThenWriteAgain) {
    Buffer<16> buf;

    // Write "abc"
    std::memcpy(buf.writeBuf(), "abc", 3);
    buf.extend(3);
    EXPECT_EQ(buf.view(), std::string_view("abc"));

    // Consume "ab"
    buf.consume(2);
    EXPECT_EQ(buf.view(), std::string_view("c"));
    EXPECT_EQ(buf.size(), 1);
    EXPECT_EQ(buf.writeSize(), 15);

    // Write "def" -> should get "cdef"
    std::memcpy(buf.writeBuf(), "def", 3);
    buf.extend(3);
    EXPECT_EQ(buf.view(), std::string_view("cdef"));
    EXPECT_EQ(buf.size(), 4);
}

TEST(BufferTest, ConsumeThenWriteFillingBuffer) {
    Buffer<8> buf;

    // Write 8 bytes, consume 3, write 3 -> should fill exactly
    std::memset(buf.writeBuf(), 'X', 8);
    buf.extend(8);
    EXPECT_EQ(buf.size(), 8);

    buf.consume(3);
    EXPECT_EQ(buf.size(), 5);
    EXPECT_EQ(buf.writeSize(), 3);

    std::memset(buf.writeBuf(), 'Y', 3);
    buf.extend(3);

    EXPECT_EQ(buf.size(), 8);
    EXPECT_EQ(buf.writeSize(), 0);
    EXPECT_EQ(buf.view().front(), 'X');  // first byte still X (shifted from index 3)
    EXPECT_EQ(buf.view().back(), 'Y');   // last bytes are Y
}

TEST(BufferTest, ConsumeZeroIsNoop) {
    Buffer<16> buf;

    std::memcpy(buf.writeBuf(), "data", 4);
    buf.extend(4);
    buf.consume(0);

    EXPECT_EQ(buf.size(), 4);
    EXPECT_EQ(buf.view(), std::string_view("data"));
}

// ===================================================================
//  extend()
// ===================================================================

TEST(BufferTest, ExtendZeroIsNoop) {
    Buffer<16> buf;

    buf.extend(0);
    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.writeSize(), 16);
}

TEST(BufferTest, ExtendIncrementsSize) {
    Buffer<16> buf;

    buf.writeBuf()[0] = 'A';
    buf.extend(1);
    EXPECT_EQ(buf.size(), 1);

    buf.writeBuf()[0] = 'B';
    buf.extend(1);
    EXPECT_EQ(buf.size(), 2);
    EXPECT_EQ(buf.view(), std::string_view("AB"));
}

// ===================================================================
//  clear()
// ===================================================================

TEST(BufferTest, ClearEmptiesBuffer) {
    Buffer<16> buf;

    std::memcpy(buf.writeBuf(), "stuff", 5);
    buf.extend(5);
    EXPECT_EQ(buf.size(), 5);

    buf.clear();
    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.writeSize(), 16);
    EXPECT_TRUE(buf.view().empty());
}

TEST(BufferTest, ClearThenReuse) {
    Buffer<16> buf;

    // First round
    std::memcpy(buf.writeBuf(), "first", 5);
    buf.extend(5);
    buf.clear();

    // Second round
    std::memcpy(buf.writeBuf(), "second", 6);
    buf.extend(6);
    EXPECT_EQ(buf.view(), std::string_view("second"));
    EXPECT_EQ(buf.size(), 6);
}

// ===================================================================
//  Edge cases
// ===================================================================

TEST(BufferTest, BufferOfSizeOne) {
    Buffer<1> buf;
    EXPECT_EQ(buf.capacity(), 1);
    EXPECT_EQ(buf.writeSize(), 1);

    buf.writeBuf()[0] = '!';
    buf.extend(1);
    EXPECT_EQ(buf.size(), 1);
    EXPECT_EQ(buf.writeSize(), 0);
    EXPECT_EQ(buf.view(), std::string_view("!"));

    buf.consume(1);
    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.writeSize(), 1);
}

TEST(BufferTest, BufferOfSizeZero) {
    Buffer<0> buf;
    EXPECT_EQ(buf.capacity(), 0);
    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.writeSize(), 0);
    EXPECT_TRUE(buf.view().empty());
}

TEST(BufferTest, RepeatedWriteConsumeCycles) {
    Buffer<32> buf;

    for (int cycle = 0; cycle < 10; ++cycle) {
        const char* payload = "ABCDEFGH";
        std::size_t len = 8;

        ASSERT_GE(buf.writeSize(), len);
        std::memcpy(buf.writeBuf(), payload, len);
        buf.extend(len);

        EXPECT_EQ(buf.view(), std::string_view("ABCDEFGH"));
        buf.consume(len);
        EXPECT_EQ(buf.size(), 0);
    }
}

TEST(BufferTest, DataPointerStaysStableAfterConsume) {
    // After consume(), data() should still point to the beginning of the
    // internal array (because of memmove).
    Buffer<32> buf;

    std::memcpy(buf.writeBuf(), "1234567890", 10);
    buf.extend(10);

    const char* original_data = buf.data();
    buf.consume(5);
    EXPECT_EQ(buf.data(), original_data);  // memmove keeps data at start
    EXPECT_EQ(buf.view(), std::string_view("67890"));
}

// ===================================================================
//  Binary data
// ===================================================================

TEST(BufferTest, BinaryDataRoundTrip) {
    Buffer<128> buf;

    std::uint8_t binary[] = {0x00, 0xFF, 0x7F, 0x80, 0x01, 0xFE};
    std::memcpy(buf.writeBuf(), binary, sizeof(binary));
    buf.extend(sizeof(binary));

    EXPECT_EQ(buf.size(), sizeof(binary));
    EXPECT_EQ(std::memcmp(buf.data(), binary, sizeof(binary)), 0);

    // Consume first 2 bytes
    buf.consume(2);
    EXPECT_EQ(buf.size(), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(buf.data()[0]), 0x7F);
}

// ===================================================================
//  Large buffer
// ===================================================================

TEST(BufferTest, LargeBufferWriteSize) {
    constexpr std::size_t N = 4096;
    Buffer<N> buf;
    EXPECT_EQ(buf.capacity(), N);
    EXPECT_EQ(buf.writeSize(), N);

    std::memset(buf.writeBuf(), 0xAB, N);
    buf.extend(N);

    EXPECT_EQ(buf.size(), N);
    EXPECT_EQ(buf.writeSize(), 0);
    EXPECT_EQ(static_cast<unsigned char>(buf.data()[0]), 0xAB);
    EXPECT_EQ(static_cast<unsigned char>(buf.data()[N - 1]), 0xAB);
}
